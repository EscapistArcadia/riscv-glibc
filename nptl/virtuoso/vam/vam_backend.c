
#include "list.h"
#include "list_t.h"
#include <stdio.h>
#include <virtuoso/vam/vam_interfaces.h>
#include <virtuoso/vam/vam_helper.h>
#include <virtuoso/vam/vam_accel_def.h>

#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/resource.h>
#include <virtuoso/pthread_types.h>
#include <virtuoso/esp/esp.h>
#include <virtuoso/esp/contig.h>


// ESP API for getting contig_alloc handle
extern contig_handle_t *lookup_handle(void *buf, enum contig_alloc_policy *policy);
// External instance of hpthread interface
extern hpthread_intf_t intf;
// Used in wakeup_vam()
pthread_t vam_th;
// Physical accelerator list
// struct physical_accel_t *accel_list = NULL;
// struct physical_accel_t *cpu_thread_list = NULL;
// Maximum loaded and minimuim loaded accel for load balancing
struct physical_accel_t *max_util_accel = NULL;
struct physical_accel_t *min_util_accel = NULL;
// Current max, min and avg util
float max_util, min_util;
// Safeguard for not performing load balance repeatedly
float load_imbalance_reg;
// Counter for core affinity
#ifdef DO_CPU_PIN
static uint8_t core_affinity_ctr = 0;
#endif
// Number of CPUs online
long cpu_online;
// Physical accelerator list
struct hpthread_cand_t *hpthread_cand_list = NULL;
#ifndef LITE_REPORT
// Number of util epochs tracked
unsigned util_epoch_count = 0;
#endif
LIST_HEAD(accel_list);
LIST_HEAD(cand_list);

void *vam_run_backend(void *arg);

// Helper function for printing hpthread primitive
const char *hpthread_get_prim_name(accel_prim_t p) {
    switch(p) {
        case PRIM_NONE : return (const char *) "NONE";
        case PRIM_AUDIO_FFT: return (const char *) "AUDIO_FFT";
        case PRIM_AUDIO_FIR: return (const char *) "AUDIO_FIR";
        case PRIM_AUDIO_FFI: return (const char *) "AUDIO_FFI";
        case PRIM_GEMM: return (const char *) "GEMM";
        default: return (const char *) "Unknown primitive";
    }
}

// Helper functions for debug
// Print function
static inline void physical_accel_dump(struct physical_accel_t *accel) {
    printf("\t- accel_id = %d\n", accel->accel_id);
    printf("\t- primitive = %s\n", hpthread_get_prim_name(accel->prim));
    printf("\t- valid_contexts = 0x%x\n", accel->valid_contexts);
    printf("\t- thread_id = ");
    for (int i = 0; i < MAX_CONTEXTS; i++)
        if (bitset_test(accel->valid_contexts, i))
            printf("%d ", accel->th[i]->accel.id);
    printf("\n");
    printf("\t- effective_util = %0.2f\n", accel->effective_util);
    printf("\t- devname = %s\n", accel->devname);
    printf("\n");
}

// Get name of the device
static inline char *physical_accel_get_name(struct physical_accel_t *accel) {
    return accel->devname;
}


// Global instance of hpthread interface
hpthread_intf_t intf;

// Helper function for swapping the state of the interface
bool hpthread_intf_swap(uint8_t expected_value, uint8_t new_value) {
    return __atomic_compare_exchange_n(&intf.state, &expected_value, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

// Helper function for testing the state of the interface
uint8_t hpthread_intf_test() {
    return __atomic_load_n(&intf.state, __ATOMIC_SEQ_CST);
}

// Helper function for setting the state of the interface
void hpthread_intf_set(uint8_t set_value) {
    __atomic_store_n(&intf.state, set_value, __ATOMIC_SEQ_CST);
}

int wakeup_vam(void) {
    cpu_online = sysconf(_SC_NPROCESSORS_ONLN);

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        perror("pthread_attr_init");
        return ENOMEM;
    }
    
    #ifdef DO_CPU_PIN
    // Set CPU affinity
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET((core_affinity_ctr++) % cpu_online, &set);
    if (pthread_attr_setaffinity_np(&attr, sizeof(set), &set) != 0) {
        perror("pthread_attr_setaffinity_np");
    }
    #endif
    #ifdef DO_SCHED_RR
    // Set SCHED_RR scheduling policy with priority 1
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO) != 0) {
        perror("pthread_attr_setschedpolicy");
    }
    struct sched_param sp = { .sched_priority = 1 };
    if (pthread_attr_setschedparam(&attr, &sp) != 0) {
        perror("pthread_attr_setschedparam");
    }
    #endif
    // Set pthread attributes to be detached; no join required
    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
        perror("attr_setdetachstate");
    }
    // Create VAM pthread
    if (pthread_create(&vam_th, &attr, vam_run_backend, NULL) != 0) {
        perror("pthread_create");
        exit(1);
    }
    pthread_attr_destroy(&attr);
    return 0;
}

void *vam_run_backend(void *arg) {
	printf("[VAM] Hello from VAM BACKEND!\n");
    #ifndef DO_SCHED_RR
    // Set niceness based on priority
    pid_t tid = syscall(__NR_gettid);
    setpriority(PRIO_PROCESS, tid, nice_table[4]);
    #endif
    // populate the list of physical accelerators in the system
    bool kill_vam = false;

    const float LB_RESET = 0.10;
    const float LB_TRIG = 0.25;
    const unsigned MAX_LB_RETRY = 3;
    unsigned NUM_LB_RETRY = MAX_LB_RETRY;
    unsigned RESET_COUNTER = 10;

    // Run loop will run forever
    while (1) {
        // Test the interface state
        uint8_t state = hpthread_intf_test();

        switch(state) {
            case VAM_IDLE: {
                // Examine the util across all accelerators in the system                
                float load_imbalance = vam_check_load_balance();
                    
                #ifndef DISABLE_LB
                bool need_load_balance = false;
                if (load_imbalance > LB_TRIG && NUM_LB_RETRY > 0) {
                    need_load_balance = true;
                } else {
                    RESET_COUNTER--;
                }

                if (RESET_COUNTER == 0) {
                    RESET_COUNTER = 10;
                    NUM_LB_RETRY = MAX_LB_RETRY;
                    if (load_imbalance > LB_RESET) {
                        need_load_balance = true;
                    }
                }

                if (need_load_balance) {
                    LOW_DEBUG(printf("[VAM] Trigerring load balancer, imbalance=%0.2f\n", load_imbalance);)
                    if (!vam_load_balance()) {
                        // If load balance was not successful, reduce retry count
                        NUM_LB_RETRY--;
                    } else {
                        // Successful load balance; reset retry count
                        NUM_LB_RETRY = MAX_LB_RETRY;
                    }
                    load_imbalance_reg = load_imbalance;
                }
                #endif
                break;
            }
            case VAM_CREATE: {
                HIGH_DEBUG(printf("[VAM] Received a request for creating hpthread %s\n", hpthread_get_name(intf.th));)
                vam_search_accel(intf.th);
                break;
            }
            case VAM_JOIN: {
                HIGH_DEBUG(printf("[VAM] Received a request for joining hpthread %s\n", hpthread_get_name(intf.th));)
                vam_release_accel(intf.th);
                break;
            }
            case VAM_SETPRIO: {
                HIGH_DEBUG(printf("[VAM] Received a request for changing priority hpthread %s to %d\n", hpthread_get_name(intf.th), intf.th->nprio);)
                vam_setprio_accel(intf.th);
                break;
            }
            case VAM_REPORT: {
                HIGH_DEBUG(printf("[VAM] Received a report request\n");)
                vam_print_report();
                kill_vam = true;
                break;
            }
            case VAM_QUERY: {
                HIGH_DEBUG(printf("[VAM] Received a query request\n");)
                intf.list = hpthread_cand_list;
                break;
            }
            default:
                break;
        }
        // If there was a request, set the state to done.
        if (state > VAM_DONE) {
            // Set the interface state to DONE
            hpthread_intf_set(VAM_DONE);
        }
        if (kill_vam) break;
        usleep(VAM_SLEEP);
    }
    return NULL;
}

void vam_search_accel(struct pthread *th) {
    HIGH_DEBUG(printf("[VAM] Searching accelerator for hpthread %s with affinity to ID %d\n", hpthread_get_name(th), th->affinity);)
    // First, update the active utilization of each accelerator
    vam_check_utilization();

    // We will find a candidate accelerator that has the lowest utiilization.
    // If no accelerator candidates are found, we will consider the CPU as the only candidate.
    struct physical_accel_t *candidate_accel = NULL;
    float candidate_util = 2.0;
    bitmap_t candidate_contexts = 255;
    bool accel_allocated = false;
    // struct physical_accel_t *cur_accel = accel_list;
    list_t *cur_node;
    struct physical_accel_t *cur_accel;

    list_for_each(cur_node, &accel_list) {
        cur_accel = list_entry(cur_node, struct physical_accel_t, node);
        HIGH_DEBUG(
            printf("\n[VAM] Checking device %s.\n", physical_accel_get_name(cur_accel));
            physical_accel_dump(cur_accel);
        )
        if (th->accel.affinity != 0 && (th->accel.affinity - 1) == cur_accel->accel_id) {
            if (cur_accel->prim == th->accel.prim && !bitset_all(cur_accel->valid_contexts)) {
                candidate_accel = cur_accel;
                candidate_util = cur_accel->effective_util;
                candidate_contexts = cur_accel->valid_contexts;
                accel_allocated = true;
                HIGH_DEBUG(printf("[VAM] Device %s matches affinity and is a candidate!\n", physical_accel_get_name(cur_accel));)
                break;
            } else if (cur_accel->prim == th->accel.prim) {
                HIGH_DEBUG(printf("[VAM] Device %s matches affinity but is not available.\n", physical_accel_get_name(cur_accel));)
            } else {
                HIGH_DEBUG(printf("[VAM] Device %s does not match primitive.\n", physical_accel_get_name(cur_accel));)
            }
        }
        // If the thread or accel requires CPU invocation, the other must too
        if (th->accel.cpu_invoke ^ cur_accel->cpu_invoke) {
		    // cur_accel = cur_accel->next;
            continue;
        }
        // Is the accelerator suitable and not fully utilized for this primitive?
        if (cur_accel->prim == th->accel.prim && !bitset_all(cur_accel->valid_contexts)) {
            // Check if this accelerator's total load is less than the previous min or fewer contexts (with similar util)
            if ((cur_accel->effective_util < candidate_util - 0.1) ||
                (fabsf(cur_accel->effective_util - candidate_util) <= 0.1 && (cur_accel->valid_contexts < candidate_contexts))) {
                candidate_accel = cur_accel;
                candidate_util = cur_accel->effective_util;
                candidate_contexts = cur_accel->valid_contexts;
                HIGH_DEBUG(printf("[VAM] Device %s is a candidate!\n", physical_accel_get_name(cur_accel));)
            }
            // Found one accelerator!
            accel_allocated = true;
        }
		// cur_accel = cur_accel->next;
    }
    HIGH_DEBUG(printf("[VAM] Candidate for hpthread %s = %s!\n", hpthread_get_name(th), physical_accel_get_name(candidate_accel));)
    // Identify the valid context to allocate
    unsigned cur_context = 0;
    if (accel_allocated) {
        for (unsigned i = 0; i < MAX_CONTEXTS; i++) {
            if (!bitset_test(candidate_accel->valid_contexts, i)) {
                cur_context = i;
                break;
            }
        }
    }
    // If no candidate accelerator was found, we will create a new CPU thread for this node.
    if (accel_allocated == false) {
        // Create a new physical accelerator for this CPU thread
        struct physical_accel_t *cpu_thread = (struct physical_accel_t *) malloc(sizeof(struct physical_accel_t));
        cpu_thread->prim = PRIM_NONE;
        LOW_DEBUG(strcpy(cpu_thread->devname, "CPU");)
        candidate_accel = cpu_thread;
        // insert_cpu_thread(cpu_thread); /* TODO: do this */
        candidate_util = 0.0;
    }
    // Update the phy<->virt mapping for the chosen context with the hpthread
    candidate_accel->th[cur_context] = th;
    th->accel.accel = candidate_accel;
    th->accel.accel_context = cur_context;
    // Mark the context as allocated.
    bitset_set(candidate_accel->valid_contexts, cur_context);
    // Configure the device allocated
    if (accel_allocated) {
        if (th->accel.cpu_invoke) {
            vam_configure_cpu_invoke(th, candidate_accel, cur_context);
        } else {
            vam_configure_accel(th, candidate_accel, cur_context);
        }
    } else {
        vam_configure_cpu(th, candidate_accel);
    }
}

void vam_configure_accel(struct pthread *th, struct physical_accel_t *accel, unsigned context) {
    HIGH_DEBUG(printf("[VAM] Configuring accel...\n");)
    // Get the mem handle for the hpthread
    void *mem = th->accel.mem;
    // ESP defined data type for the pointer to the memory pool for accelerators.
    enum contig_alloc_policy policy;
    contig_handle_t *handle = lookup_handle(mem, &policy);

    // Configring all device-independent fields in the esp desc
    struct esp_access *esp_access_desc = (struct esp_access *) accel->esp_access_desc;
    {
        esp_access_desc->contig = contig_to_khandle(*handle);
        esp_access_desc->ddr_node = contig_to_most_allocated(*handle);
        esp_access_desc->alloc_policy = policy;
        esp_access_desc->run = true;
        esp_access_desc->coherence = ACC_COH_RECALL;
        esp_access_desc->spandex_conf = 0;
        esp_access_desc->start_stop = 1;
        esp_access_desc->p2p_store = 0;
        esp_access_desc->p2p_nsrcs = 0;
        esp_access_desc->src_offset = 0;
        esp_access_desc->dst_offset = 0;
        esp_access_desc->context_id = context;
        esp_access_desc->context_queue_ptr = th->accel.queue_ptr;
        esp_access_desc->context_nprio = th->accel.nprio;
        esp_access_desc->valid_contexts = accel->valid_contexts;
        esp_access_desc->sched_period = AVU_SCHED_PERIOD;
    }

    if (accel->init_done) {
        LOW_DEBUG(printf("[VAM] Adding to accel %s:%d for hpthread %s\n", physical_accel_get_name(accel), context, hpthread_get_name(th));)
        esp_access_desc->ioctl_cm = ESP_IOCTL_ACC_ADD_CONTEXT;
    } else {
        LOW_DEBUG(printf("[VAM] Initializing accel %s:%d for hpthread %s\n", physical_accel_get_name(accel), context, hpthread_get_name(th));)
        esp_access_desc->ioctl_cm = ESP_IOCTL_ACC_INIT;
    }
    if (ioctl(accel->fd, accel->ioctl_cm, esp_access_desc)) {
        perror("ioctl");
        exit(EXIT_FAILURE);
    }    
    accel->init_done = true;
    // Read the current time for when the accelerator is started.
    accel->context_start_cycles[context] = get_counter();
    accel->context_active_cycles[context] = 0;
}

#ifdef DO_PER_INVOKE
void vam_configure_cpu_invoke(struct pthread *th, struct physical_accel_t *accel, unsigned context) {
    LOW_DEBUG(printf("[VAM] Launch CPU invoke thread for hpthread %s on %s:%d\n", hpthread_get_name(th), physical_accel_get_name(accel), context);)
    struct cpu_invoke_args_t *args = accel->args[context];
    args->context = context;
    args->active_cycles = 0;
    args->kill_pthread = false;
    args->accel = accel;
    // Find SW kernel for this thread
    void *(*sw_kernel)(void *);
    switch(th->accel.prim) {
        case PRIM_GEMM: sw_kernel = gemm_invoke; break;
        default: break;
    }    
    // Create a new CPU thread for the SW implementation of this node.
    pthread_t cpu_thread;
    // Create pthread attributes
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        perror("attr_init");
    }
    #ifdef DO_CPU_PIN
    // Set CPU affinity
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET((core_affinity_ctr++) % cpu_online, &set);
    if (pthread_attr_setaffinity_np(&attr, sizeof(set), &set) != 0) {
        perror("pthread_attr_setaffinity_np");
    }
    #endif
    #ifdef DO_SCHED_RR
    // Set SCHED_RR scheduling policy with priority 1
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO) != 0) {
        perror("pthread_attr_setschedpolicy");
    }
    struct sched_param sp = { .sched_priority = 1 };
    if (pthread_attr_setschedparam(&attr, &sp) != 0) {
        perror("pthread_attr_setschedparam");
    }
    #endif
    if (pthread_create(&cpu_thread, &attr, sw_kernel, (void *) args) != 0) {
        perror("Failed to create CPU thread\n");
    }
    pthread_attr_destroy(&attr);

    // Add this thread to the physical_accel struct
    accel->cpu_thread[context] = cpu_thread;
}
#else
void vam_configure_cpu_invoke(struct pthread *th, struct physical_accel_t *accel, unsigned context) {
    if (accel->init_done) {
        LOW_DEBUG(printf("[VAM] Added hpthread %s to context %d of invoke thread on %s\n", hpthread_get_name(th), context, physical_accel_get_name(accel));)
        bitset_reset(accel->args->valid_contexts_ack, context);
    } else {
        LOW_DEBUG(printf("[VAM] Launch CPU invoke thread for hpthread %s on %s\n", hpthread_get_name(th), physical_accel_get_name(accel));)
        bitset_reset_all(accel->args->valid_contexts_ack);
        for (int i = 0; i < MAX_CONTEXTS; i++) {
            accel->args->active_cycles[i] = 0;
        }
        accel->args->kill_pthread = false;
        // Find SW kernel for this thread
        void *(*sw_kernel)(void *);
        switch(th->accel.prim) {
            case PRIM_GEMM: sw_kernel = gemm_invoke; break;
            default: break;
        }    
        // Create a new CPU thread for the SW implementation of this node.
        pthread_t cpu_thread;
        // Create pthread attributes
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) != 0) {
            perror("attr_init");
        }
        #ifdef DO_CPU_PIN
        // Set CPU affinity
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET((core_affinity_ctr++) % cpu_online, &set);
        if (pthread_attr_setaffinity_np(&attr, sizeof(set), &set) != 0) {
            perror("pthread_attr_setaffinity_np");
        }
        #endif
        #ifdef DO_SCHED_RR
        // Set SCHED_RR scheduling policy with priority 1
        if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO) != 0) {
            perror("pthread_attr_setschedpolicy");
        }
        struct sched_param sp = { .sched_priority = 1 };
        if (pthread_attr_setschedparam(&attr, &sp) != 0) {
            perror("pthread_attr_setschedparam");
        }
        #endif
        if (pthread_create(&cpu_thread, &attr, sw_kernel, (void *) accel) != 0) {
            perror("Failed to create CPU thread\n");
        }
        pthread_attr_destroy(&attr);

        // Add this thread to the physical_accel struct
        accel->cpu_thread = cpu_thread;
        accel->init_done = true;
    }
}
#endif

void vam_configure_cpu(struct pthread *th, struct physical_accel_t *accel) {
    LOW_DEBUG(printf("[VAM] Configuring CPU for hpthread %s\n", hpthread_get_name(th));)
    // Find SW kernel for this thread
    void *(*sw_kernel)(void *);
    switch(th->accel.prim) {
        case PRIM_GEMM: sw_kernel = NULL; break; /* TODO: passed routine */
        default: break;
    }    
    // Create a new CPU thread for the SW implementation of this node.
    pthread_t cpu_thread;
    th->accel.kill_pthread = (bool *) malloc (sizeof(bool)); *(th->accel.kill_pthread) = false;
    if (pthread_create(&cpu_thread, NULL, sw_kernel, (void *) &th->accel) != 0) {
        perror("Failed to create CPU thread\n");
    }

    // Add this thread to the physical_accel struct
#ifdef DO_PER_INVOKE
    accel->cpu_thread[0] = cpu_thread;
#else
    accel->cpu_thread = cpu_thread;
#endif
}

void vam_release_accel(struct pthread *th) {
    struct physical_accel_t *accel = th->accel.accel;
    unsigned context = th->accel.accel_context;
    LOW_DEBUG(printf("[VAM] Releasing accel %s:%d for hpthread %s\n", physical_accel_get_name(accel), context, hpthread_get_name(th));)
    // Free the allocated context.
    bitset_reset(accel->valid_contexts, context);

    if (th->accel.cpu_invoke) {
#ifdef DO_PER_INVOKE
        accel->args[context]->kill_pthread = true;
        pthread_join(accel->cpu_thread[context], NULL);
#else
        while(bitset_test(accel->args->valid_contexts_ack, context)) {
            SCHED_YIELD;
        }
#endif
    } else {
        if (accel->prim == PRIM_NONE) {
            *(th->accel.kill_pthread) = true;
#ifdef DO_PER_INVOKE
            pthread_join(accel->cpu_thread[0], NULL);
#else
            pthread_join(accel->cpu_thread, NULL);
#endif
        } else {
            struct esp_access *esp_access_desc = accel->esp_access_desc;
            {
                esp_access_desc->context_id = context;
                esp_access_desc->valid_contexts = accel->valid_contexts;
                esp_access_desc->ioctl_cm = ESP_IOCTL_ACC_DEL_CONTEXT;
            }
            if (ioctl(accel->fd, accel->ioctl_cm, esp_access_desc)) {
                perror("ioctl");
                exit(EXIT_FAILURE);
            }
            // If there is no context active, we should re-init the accelerator the next time
            if (bitset_none(accel->valid_contexts)) {
                accel->init_done = false;
            }
        }
    }

    // Delete the entry for this context in the phy<->virt mapping
    accel->th[context] = NULL;
    th->accel.accel = NULL;
}

void vam_setprio_accel(struct pthread *th) {
    struct physical_accel_t *accel = th->accel.accel;
    unsigned context = th->accel.accel_context;
    LOW_DEBUG(printf("[VAM] Setting priority of accel %s:%d to %d for hpthread %s\n", physical_accel_get_name(accel), context, th->accel.nprio, hpthread_get_name(th));)

    struct esp_access *esp_access_desc = accel->esp_access_desc;
    {
        esp_access_desc->context_id = context;
        esp_access_desc->context_nprio = th->accel.nprio;
        esp_access_desc->ioctl_cm = ESP_IOCTL_ACC_SET_PRIO;
    }
    if (ioctl(accel->fd, accel->ioctl_cm, esp_access_desc)) {
        perror("ioctl");
        exit(EXIT_FAILURE);
    }
}

// void insert_physical_accel(struct physical_accel_t *accel) {
//     accel->next = NULL;
//     if (!accel_list) {
//         accel_list = accel;
//         return;
//     }
//     struct physical_accel_t *p = accel_list;
//     while (p->next) p = p->next;
//     p->next = accel;
// }

// void insert_hpthread_cand(hpthread_cand_t *cand) {
//     cand->next = hpthread_cand_list;
//     hpthread_cand_list = cand;
// }

// void insert_cpu_thread(struct physical_accel_t *accel) {
// 	accel->next = cpu_thread_list;
// 	cpu_thread_list = accel;
// }

void vam_check_utilization() {
    // struct physical_accel_t *cur_accel = accel_list;
    list_t *cur_node;
    struct physical_accel_t *cur_accel;
    list_for_each(cur_node, &accel_list) {
        cur_accel = list_entry(cur_node, struct physical_accel_t, node);
        struct avu_mon_desc mon;
        uint64_t *mon_extended = (uint64_t *) mon.util;
        HIGH_DEBUG(
            printf("[VAM] Util of %s: ", physical_accel_get_name(cur_accel));
        )
        if (cur_accel->cpu_invoke) {
            for (int i = 0; i < MAX_CONTEXTS; i++) {
                #ifdef DO_PER_INVOKE
                mon_extended[i] = cur_accel->args[i]->active_cycles;
                #else
                mon_extended[i] = cur_accel->args->active_cycles[i];
                #endif
            }
        } else {
            if (ioctl(cur_accel->fd, ESP_IOC_MON, &mon)) {
                perror("ioctl");
                exit(EXIT_FAILURE);
            }
        }
        cur_accel->effective_util = 0;

        for (int i = 0; i < MAX_CONTEXTS; i++) {
            if (bitset_test(cur_accel->valid_contexts, i)) {
                // Get the utilization in the previous monitor period
                uint64_t elapsed_cycles = get_counter() - cur_accel->context_start_cycles[i];
                uint64_t util_cycles = mon_extended[i] - cur_accel->context_active_cycles[i];
                float util = (float) util_cycles/elapsed_cycles;
                // Set the cycles for the next period
                cur_accel->context_start_cycles[i] = get_counter();
                cur_accel->context_active_cycles[i] = mon_extended[i];
                cur_accel->context_util[i] = util;
                struct pthread *th = cur_accel->th[i];
                th->accel.th_util = util;
                cur_accel->effective_util += util / th->accel.nprio;

                HIGH_DEBUG(
                    printf("C%d(%d)=%05.2f%%, ", i, th->accel.nprio, util * 100);
                )
            }
        }
        HIGH_DEBUG(printf("e.util=%05.2f%%\n", cur_accel->effective_util * 100);)
		// cur_accel = cur_accel->next;
    }
}

float vam_check_load_balance() {
    // First, update the active utilization of each accelerator
    vam_check_utilization();
    // Check whether there is load imbalance across accelerators
    // - calculate average and max/min util across all accelerators
    float local_max_util = 0.0; float local_min_util = 10.0;
    bool skip_load_balance = true;

    list_t *cur_node;
    struct physical_accel_t *cur_accel;
    struct physical_accel_t *tmp_max, *tmp_min;
    list_for_each(cur_node, &accel_list) {
        cur_accel = list_entry(cur_node, struct physical_accel_t, node);
        float cur_util = cur_accel->effective_util;
        if (cur_util < local_min_util) { local_min_util = cur_util; tmp_min = cur_accel; }
        if (cur_util > local_max_util) { local_max_util = cur_util; tmp_max = cur_accel; }
        if (bitset_count(cur_accel->valid_contexts) > 1) skip_load_balance = false;
        HIGH_DEBUG(printf("[VAM] Current load of %s = %0.2f\n", physical_accel_get_name(cur_accel), cur_util);)
        // cur_accel = cur_accel->next;
    }
    // If none of the accelerators have more than one valid context, there's no need for load balancing
    if (skip_load_balance) return 0.0;

    HIGH_DEBUG(printf("[VAM] Max util = %0.2f, min util = %0.2f\n", local_max_util, local_min_util);)
    max_util_accel = tmp_max; min_util_accel = tmp_min;
    max_util = local_max_util; min_util = local_min_util; 
    return local_max_util - local_min_util;
}

bool vam_load_balance() {
    // Contexts we are migrating (swap only if both accel are full)
    unsigned best_context_min, best_context_max; 
    struct pthread *best_th_max = NULL; struct pthread *any_th_max = NULL;
    struct pthread *best_th_min = NULL; struct pthread *any_th_min = NULL;
    struct pthread *move_th_max, *move_th_min;
    float best_util_max = 0.0; float any_util_max = 0.0;
    float best_util_min = 10.0; float any_util_min = 10.0;
    bool no_min_contexts = false;
    // Find the most loaded thread on most loaded accel that was not recently moved
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        if (bitset_test(max_util_accel->valid_contexts, i)) {
            struct pthread *th = max_util_accel->th[i];
            uint64_t t_last_move = get_counter() - th->accel.th_last_move;
            float th_util = th->accel.th_util / th->accel.nprio;
            if (th_util > any_util_max) {
                any_th_max = th;
                any_util_max = th_util;
            }
            if (th_util > best_util_max && t_last_move > TH_MOVE_COOLDOWN) {
                best_th_max = th;
                best_util_max = th_util;
            }
        }
    }
    move_th_max = best_th_max ? best_th_max : any_th_max;
    best_context_max = move_th_max->accel.accel_context;
    move_th_max->accel.th_last_move = get_counter();

    // Check if there exist any valid contexts on least loaded accel
    if (bitset_all(min_util_accel->valid_contexts)) {
        // if not, we need to release the least loaded thread on it.
        for (int i = 0; i < MAX_CONTEXTS; i++) {
            if (bitset_test(min_util_accel->valid_contexts, i)) {
                struct pthread *th = min_util_accel->th[i];
                uint64_t t_last_move = get_counter() - th->accel.th_last_move;
                float th_util = th->accel.th_util / th->accel.nprio;
                if (th_util < any_util_min) {
                    any_th_min = th;
                    any_util_min = th_util;
                }
                if (th_util < best_util_min && t_last_move > TH_MOVE_COOLDOWN) {
                    best_th_min = th;
                    best_util_min = th_util;
                }
            }
        }
        move_th_min = best_th_min ? best_th_min : any_th_min;
        best_context_min = move_th_min->accel.accel_context;
        move_th_min->accel.th_last_move = get_counter();
        no_min_contexts = true;
    } else {
        // if yes, identify one context to allocate
        for (unsigned i = 0; i < MAX_CONTEXTS; i++) {
            if (!bitset_test(min_util_accel->valid_contexts, i)) {
                best_context_min = i;
                break;
            }
        }
    }

    // Roughly check if the util improvement is worth the migration
    float local_max_util = max_util;
    float local_min_util = min_util;
    local_max_util -= move_th_max->accel.th_util / move_th_max->accel.nprio; local_min_util += move_th_max->accel.th_util / move_th_max->accel.nprio;
    LOW_DEBUG(printf("[VAM] Map %s from %s:%d to %s:%d\n", hpthread_get_name(move_th_max),
                        physical_accel_get_name(max_util_accel), best_context_max, physical_accel_get_name(min_util_accel), best_context_min);)
    if (no_min_contexts) {
        local_max_util += move_th_min->accel.th_util / move_th_min->accel.nprio; local_min_util -= move_th_min->accel.th_util / move_th_min->accel.nprio;
        LOW_DEBUG(printf("[VAM] Map %s from %s:%d to %s:%d\n", hpthread_get_name(move_th_min),
                            physical_accel_get_name(min_util_accel), best_context_min, physical_accel_get_name(max_util_accel), best_context_max);)
    }
    float old_load_imbalance = max_util - min_util;
    float new_load_imbalance = fabsf(local_max_util - local_min_util);
    const float LB_RETRY_DIFF = 0.10;
    if (old_load_imbalance - new_load_imbalance < LB_RETRY_DIFF) {
        load_imbalance_reg = old_load_imbalance;
        LOW_DEBUG(printf("[VAM] Skipping load balance, new imbalance = %f\n", new_load_imbalance);)
        return false;
    }

    // Release the context first
    vam_release_accel(move_th_max);
    if (no_min_contexts) {
        // Release the least loaded context, if needed
        vam_release_accel(move_th_min);
    }

    // Update the phy<->virt mapping for the chosen context with the hpthread
    min_util_accel->th[best_context_min] = move_th_max;
    move_th_max->accel.accel = min_util_accel;
    move_th_max->accel.accel_context = best_context_min;
    // Mark the context as allocated.
    bitset_set(min_util_accel->valid_contexts, best_context_min);
    // Configure the device allocated
    if (move_th_max->accel.cpu_invoke) {
        vam_configure_cpu_invoke(move_th_max, min_util_accel, best_context_min);
    } else {
        vam_configure_accel(move_th_max, min_util_accel, best_context_min);
    }

    if (no_min_contexts) {
        // Update the phy<->virt mapping for the chosen context with the hpthread
        max_util_accel->th[best_context_max] = move_th_min;
        move_th_min->accel.accel = max_util_accel;
        move_th_min->accel.accel_context = best_context_max;
        // Mark the context as allocated.
        bitset_set(max_util_accel->valid_contexts, best_context_max);
        // Configure the device allocated
        if (move_th_min->accel.cpu_invoke) {
            vam_configure_cpu_invoke(move_th_min, max_util_accel, best_context_max);
        } else {
            vam_configure_accel(move_th_min, max_util_accel, best_context_max);
        }
    }
    return true;
}

void vam_log_utilization() {
#ifdef LITE_REPORT    
    struct physical_accel_t *cur_accel = accel_list;
    while (cur_accel != NULL) {
        HIGH_DEBUG( printf("[VAM] Logging utilization for %s\n", physical_accel_get_name(cur_accel)); )
        if (cur_accel->util_entry_list == NULL) {
            cur_accel->util_entry_list = (util_entry_t *) malloc (sizeof(util_entry_t));
            for (int i = 0; i < MAX_CONTEXTS; i++) {
                cur_accel->util_entry_list->util[i] = 0.0;
                cur_accel->util_entry_list->id[i] = 0;
            }
            cur_accel->util_entry_list->util_epoch_count = 0;
        }
        util_entry_t *entry = cur_accel->util_entry_list;
        for (int i = 0; i < MAX_CONTEXTS; i++) {
            if (bitmap_test(cur_accel->valid_contexts, i)) {
                entry->util[i] += cur_accel->context_util[i];
            }
        }
        if (bitset_any(cur_accel->valid_contexts)) entry->util_epoch_count++;
        cur_accel = cur_accel->next;
    }
#else    
    // struct physical_accel_t *cur_accel = accel_list;
    list_t *cur_node;
    struct physical_accel_t *cur_accel;
    list_for_each(cur_node, &accel_list) {
        cur_accel = list_entry(cur_node, struct physical_accel_t, node);
        LOW_DEBUG( printf("[VAM] Logging utilization for %s: ", physical_accel_get_name(cur_accel)); )
        util_entry_t *new_entry = (util_entry_t *) malloc (sizeof(util_entry_t));
        float total_util = 0.0;
        for (int i = 0; i < MAX_CONTEXTS; i++) {
            if (bitset_test(cur_accel->valid_contexts, i)) {
                new_entry->util[i] = cur_accel->context_util[i];
                new_entry->id[i] = cur_accel->th[i]->accel.user_id;
                LOW_DEBUG( printf("C%d(%d)=%05.2f%%, ", i, new_entry->id[i], new_entry->util[i] * 100); )
                total_util += cur_accel->context_util[i];
            } else {
                new_entry->util[i] = 0.0;
                new_entry->id[i] = 0;
                LOW_DEBUG( printf("C%d(-)=--.--%%, ", i); )
            }
        }
        LOW_DEBUG( printf("total=%05.2f%%, e.util=%05.2f%%\n", total_util * 100, cur_accel->effective_util * 100); )
        // Add new entry to the front of the util list
        new_entry->next = cur_accel->util_entry_list;
        cur_accel->util_entry_list = new_entry;
        // cur_accel = cur_accel->next;
    }
    util_epoch_count++;
#endif
}

void vam_print_report() {
#ifdef LITE_REPORT
    struct physical_accel_t *cur_accel = accel_list;
    while (cur_accel != NULL) {
        printf("[FILTER] ");
        util_entry_t *entry = cur_accel->util_entry_list;
        // Calculate average utilization for each accelerator
        float total_util = 0.0;
        for (int j = 0; j < MAX_CONTEXTS; j++) {
            printf("%05.2f%%, ", (entry->util[j]*100)/entry->util_epoch_count);
            total_util += entry->util[j];
        }
        printf("total: %05.2f%%\n", (total_util*100)/entry->util_epoch_count);
        cur_accel = cur_accel->next;
    }
#elif MED_REPORT  
    for (int i = 0; i < util_epoch_count; i++) {
        printf("[FILTER] ");
        struct physical_accel_t *cur_accel = accel_list;
        while (cur_accel != NULL) {
            // printf("%s.%d: ", hpthread_get_prim_name(cur_accel->prim), cur_accel->accel_id);
            float total_util = 0.0;
            util_entry_t *entry = cur_accel->util_entry_list;
            util_entry_t *prev = NULL;
            // Traverse to the end of the list to get the oldest entry
            while (entry->next != NULL) {
                prev = entry;
                entry = entry->next;
            }
            for (int j = 0; j < MAX_CONTEXTS; j++) {
                total_util += entry->util[j];
            }
            #if !defined(ENABLE_VAM) || defined(ENABLE_MOZART)
            printf("%05.2f%%(%d), ", total_util*100, entry->id[0]);
            #else
            printf("%05.2f%%, ", total_util*100);
            #endif
            // Delete the oldest entry after printing
            if (prev != NULL) {
                prev->next = NULL;
            } else {
                cur_accel->util_entry_list = NULL;
            }
            free(entry);
            cur_accel = cur_accel->next;
        }
        printf("\n");
    }
#else    
    // Print out the total utilization
    for (int i = 0; i < util_epoch_count; i++) {
        // struct physical_accel_t *cur_accel = accel_list;
        list_t *cur_node;
        struct physical_accel_t *cur_accel;
        list_for_each(cur_node, &accel_list) {
            cur_accel = list_entry(cur_node, struct physical_accel_t, node);
            printf("%s ", physical_accel_get_name(cur_accel));
            util_entry_t *entry = cur_accel->util_entry_list;
            util_entry_t *prev = NULL;
            // Traverse to the end of the list to get the oldest entry
            while (entry->next != NULL) {
                prev = entry;
                entry = entry->next;
            }
            for (int j = 0; j < MAX_CONTEXTS; j++) {
                printf("%05.2f%%(%d) ", entry->util[j]*100, entry->id[j]);
            }
            printf("\n");
            // Delete the oldest entry after printing
            if (prev != NULL) {
                prev->next = NULL;
            } else {
                cur_accel->util_entry_list = NULL;
            }
            free(entry);
            // cur_node = cur_node->next;
        }
    }
#endif    
}
