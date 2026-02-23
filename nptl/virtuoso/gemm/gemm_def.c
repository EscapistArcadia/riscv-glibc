#define _GNU_SOURCE
#include <stdlib.h>
#include <sched.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <limits.h>
#include <hpthread.h>
#include <common_helper.h>
#include <vam_physical_accel.h>
#include <gemm_params.h>
#include <gemm_def.h>
#include <gemm_stratus.h>
#include <gemm_sm_stratus.h>
#include <libesp.h>
#include <esp.h>
#include <esp_accelerator.h>
#include <nn_token.h>

// ESP API for getting contig_alloc handle
extern contig_handle_t *lookup_handle(void *buf, enum contig_alloc_policy *policy);

#ifndef ENABLE_VAM
static bool gemm_allocated[10] = {false}; // Arbitrary number of accel

bool allocate_gemm_accel(unsigned accel_idx) {
    bool expected_value = false;
    HIGH_DEBUG(printf("[INVOKE] Attempting to allocate gemm_stratus.%d\n", accel_idx);)
    return __atomic_compare_exchange_n(&gemm_allocated[accel_idx], &expected_value, true, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
#endif

// Device-dependent probe function for baseline accelerator
void gemm_probe(physical_accel_t *accel) {
    accel->prim = PRIM_GEMM;
    accel->cpu_invoke = true;
}

// Device-dependent probe function for SM accelerator
void gemm_sm_probe(physical_accel_t *accel) {
    accel->prim = PRIM_GEMM;
    accel->cpu_invoke = false;
    accel->ioctl_cm = GEMM_SM_STRATUS_IOC_ACCESS;

    // Create a new access struct and track within the device struct
    struct gemm_sm_stratus_access *gemm_desc = (struct gemm_sm_stratus_access *) malloc (sizeof(struct gemm_sm_stratus_access));
    accel->esp_access_desc = (struct esp_access *) gemm_desc;
}

void *gemm_invoke(void *a) {
#ifdef DO_PER_INVOKE
    cpu_invoke_args_t *args = (cpu_invoke_args_t *) a;
    physical_accel_t *accel = args->accel;
    unsigned context = args->context;
    bool *kill_pthread = &args->kill_pthread;
    uint64_t *context_runtime = &args->active_cycles; // for VAM
    hpthread_t *th = accel->th[context];
    hpthread_args_t *h_args = th->args;
    unsigned *mem = (unsigned *) h_args->mem;
    sm_queue_t *q = (sm_queue_t *) &mem[h_args->queue_ptr];
    LOW_DEBUG(printf("[INVOKE] Started thread for invoking GeMM on %s:%d!\n", accel->devname, context);)
    // Set queue to busy
    if (__atomic_load_n(&(q->stat), __ATOMIC_SEQ_CST) == QUEUE_BUSY) { SCHED_YIELD; };
    __atomic_store_n(&(q->stat), QUEUE_BUSY, __ATOMIC_SEQ_CST);
    HIGH_DEBUG(printf("[INVOKE] Queue set to busy on %s:%d!\n", accel->devname, context);)

    #ifndef DO_SCHED_RR
    // Set niceness based on priority
    pid_t tid = syscall(SYS_gettid);
    unsigned prio = th->nprio;
    setpriority(PRIO_PROCESS, tid, nice_table[prio - 1]);
    HIGH_DEBUG(printf("[INVOKE] Set niceness to %d for %s:%d!\n", nice_table[prio - 1], accel->devname, context);)
    #endif

    // Set up local descriptors for each context ahead of time
    struct gemm_stratus_access *gemm_access_desc;
    gemm_access_desc = (struct gemm_stratus_access *) malloc (sizeof(struct gemm_stratus_access));
	// Setting up ESP memory buffer
    enum contig_alloc_policy policy;
    contig_handle_t *handle = lookup_handle((void*) mem, &policy);
    gemm_access_desc->esp.contig = contig_to_khandle(*handle);
    gemm_access_desc->esp.ddr_node = contig_to_most_allocated(*handle);
    gemm_access_desc->esp.alloc_policy = policy;
    gemm_access_desc->esp.run = true;
    gemm_access_desc->esp.src_offset = 0;
    gemm_access_desc->esp.dst_offset = 0;
    gemm_access_desc->esp.coherence = ACC_COH_RECALL;
    gemm_access_desc->esp.start_stop = 0;
    gemm_access_desc->esp.p2p_store = 0;
    gemm_access_desc->esp.p2p_nsrcs = 0;
    gemm_access_desc->esp.ioctl_cm = ESP_IOCTL_ACC_NO_SM;

    HIGH_DEBUG(unsigned invoke_count = 0;)

    while (1) {
        if (*kill_pthread) { 
            __atomic_store_n(&(q->stat), QUEUE_AVAIL, __ATOMIC_SEQ_CST);
            pthread_exit(NULL);
        }
        // Is task queue empty?
        if (!sm_queue_empty(q)) {
            // Read descriptor from tail
            unsigned descr_offset = sm_queue_can_pop(q);
            gemm_queue_entry_t *e = (gemm_queue_entry_t *) &mem[descr_offset];
            gemm_params_t *params = &(e->gemm_params);
            gemm_access_desc->dim_m = params->dim_m;
            gemm_access_desc->dim_n = params->dim_n;
            gemm_access_desc->dim_k = params->dim_k;
            gemm_access_desc->weight_base = params->weight_base;
            gemm_access_desc->input_base = params->input_base;
            gemm_access_desc->output_base = params->output_base;

            // Wait for output queue to be not full
            sm_queue_t *output_queue = (sm_queue_t *) &(mem[e->common.output_queue]);
            uint64_t output_entry = e->common.output_entry;
            sm_queue_pop(q);
            while(sm_queue_full(output_queue)) { SCHED_YIELD; }
            // Acquire ioctl lock
            unsigned expected_value = 0;
            while(!__atomic_compare_exchange_n(&accel->accel_lock, &expected_value, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { 
                expected_value = 0;
                SCHED_YIELD;
            }
            HIGH_DEBUG(printf("[INVOKE] Starting GEMM %d on %s:%d\n", invoke_count, accel->devname, context);)

            struct esp_access *esp_access_desc = (struct esp_access *) gemm_access_desc;
            if (ioctl(accel->fd, GEMM_STRATUS_IOC_ACCESS, esp_access_desc)) {
                perror("ioctl");
                exit(EXIT_FAILURE);
            }
            // Push to output queue
            sm_queue_push(output_queue, output_entry);
            uint64_t *mon_extended = (uint64_t *) esp_access_desc->mon_info.util;
            *context_runtime += mon_extended[0]; // Single context only
            __atomic_store_n(&accel->accel_lock, 0, __ATOMIC_RELEASE);
            HIGH_DEBUG(printf("[INVOKE] Finished GEMM %d on %s:%d\n", invoke_count++, accel->devname, context);)
        }
        SCHED_YIELD;
    }
#else
    // Read the arguments struct
    physical_accel_t *accel = (physical_accel_t *) a;
    cpu_invoke_args_t *args = accel->args;
    HIGH_DEBUG(printf("[INVOKE] Started invoke thread on %s\n", accel->devname);)
    bool *kill_pthread = &args->kill_pthread;
    bitset_t *valid_contexts_ack = &args->valid_contexts_ack;
    uint64_t *context_runtime = args->active_cycles; // for VAM
    uint64_t context_vruntime[MAX_CONTEXTS] = {0}; // for local multiplexing
    unsigned vruntime_scale[MAX_CONTEXTS] = {1}; // to penalize idle threads
    struct timespec start_time, stop_time;                     
    uint64_t sched_period_elapsed = get_counter();
    const uint64_t SCHED_PERIOD = 7812500; // 100ms
    unsigned current_context = 0;
    hpthread_t **th = accel->th;
    HIGH_DEBUG(unsigned invoke_count[MAX_CONTEXTS] = {0};)
    // Set up local descriptors for each context ahead of time
    struct gemm_stratus_access **gemm_access_desc;
    gemm_access_desc = (struct gemm_stratus_access **) malloc (MAX_CONTEXTS * sizeof(struct gemm_stratus_access *));
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        gemm_access_desc[i] = (struct gemm_stratus_access *) malloc (sizeof(struct gemm_stratus_access));
    }

    #ifndef DO_SCHED_RR
    // Set niceness based on priority
    pid_t tid = syscall(SYS_gettid);
    setpriority(PRIO_PROCESS, tid, nice_table[0]);
    HIGH_DEBUG(printf("[INVOKE] Set niceness to %d for %s!\n", nice_table[0], accel->devname);)
    #endif

    while (1) {
        // Check if we need to exit
        if (*kill_pthread) {
            HIGH_DEBUG(printf("[INVOKE] Terminating invoke thread on %s\n", accel->devname);)
            pthread_exit(NULL);
        }
        // Check for old contexts to remove
        for (int i = 0; i < MAX_CONTEXTS; i++) {
            if (!bitset_test(accel->valid_contexts, i) && bitset_test(*valid_contexts_ack, i)) {
                hpthread_args_t *h_args = th[i]->args;
                unsigned *mem = (unsigned *) h_args->mem;
                sm_queue_t *q = (sm_queue_t *) &mem[h_args->queue_ptr];
                __atomic_store_n(&(q->stat), QUEUE_AVAIL, __ATOMIC_SEQ_CST);
                bitset_reset(*valid_contexts_ack, i);
                HIGH_DEBUG(printf("[INVOKE] Released context %d on %s for hpthread %s\n", i, accel->devname, hpthread_get_name(th[i]));)
            }
        }
        // Ensure there is at least one valid context
        if (bitset_none(accel->valid_contexts)) {
            SCHED_YIELD;
            continue;
        }
        // Iterate through all the contexts and find the lowest active cycles for this scheduling period
        uint64_t min_vruntime = UINT64_MAX;
        unsigned min_nprio = INT_MAX;
        unsigned new_context = (current_context + 1) % MAX_CONTEXTS;
        for (int i = 0; i < MAX_CONTEXTS; i++) {
            if (!bitset_test(accel->valid_contexts, new_context)) {
                context_vruntime[new_context] = UINT64_MAX;
                vruntime_scale[new_context] = 1;
            } else {
                unsigned nprio = th[new_context]->nprio;
                if (bitset_test(accel->valid_contexts, new_context) && nprio < min_nprio && context_vruntime[new_context] == min_vruntime) {
                    min_nprio = nprio;
                    current_context = new_context;
                } else if (bitset_test(accel->valid_contexts, new_context) && context_vruntime[new_context] < min_vruntime) {
                    min_vruntime = context_vruntime[new_context];
                    min_nprio = nprio;
                    current_context = new_context;
                }
            }
            new_context = (new_context + 1) % MAX_CONTEXTS;
        }
        HIGH_DEBUG(
            if (bitset_count(accel->valid_contexts) > 1)
                printf("[INVOKE] Selected context %d on %s\n", current_context, accel->devname);
        )
        // Check for new contexts to add
        for (int i = 0; i < MAX_CONTEXTS; i++) {
            if (bitset_test(accel->valid_contexts, i) && !bitset_test(*valid_contexts_ack, i)) {
                hpthread_args_t *h_args = th[i]->args;
                unsigned *mem = (unsigned *) h_args->mem;
                sm_queue_t *q = (sm_queue_t *) &mem[h_args->queue_ptr];
                if (__atomic_load_n(&(q->stat), __ATOMIC_SEQ_CST) == QUEUE_BUSY) { SCHED_YIELD; continue; };
                __atomic_store_n(&(q->stat), QUEUE_BUSY, __ATOMIC_SEQ_CST);
                bitset_set(*valid_contexts_ack, i);
                context_vruntime[i] = min_vruntime + 1; // Initialize vruntime
                // We will populate the common fields of esp_access
                enum contig_alloc_policy policy;
                contig_handle_t *handle = lookup_handle((void*) mem, &policy);
                gemm_access_desc[i]->esp.contig = contig_to_khandle(*handle);
                gemm_access_desc[i]->esp.ddr_node = contig_to_most_allocated(*handle);
                gemm_access_desc[i]->esp.alloc_policy = policy;
                gemm_access_desc[i]->esp.run = true;
                gemm_access_desc[i]->esp.src_offset = 0;
                gemm_access_desc[i]->esp.dst_offset = 0;
                gemm_access_desc[i]->esp.coherence = ACC_COH_RECALL;
                HIGH_DEBUG(printf("[INVOKE] Added context %d on %s for hpthread %s\n", i, accel->devname, hpthread_get_name(th[i]));)
            }
        }
        // Scheduling period elapsed
        if (get_counter() - sched_period_elapsed >= SCHED_PERIOD) {
            // Reset all vruntime counters
            for (int i = 0; i < MAX_CONTEXTS; i++) {
                context_vruntime[i] = 0;
            }
            sched_period_elapsed = get_counter();
            HIGH_DEBUG(printf("[INVOKE] Scheduling period elapsed for %s\n", accel->devname);)
        }
        // Read arguments for next context
        hpthread_args_t *h_args = th[current_context]->args;
        unsigned *mem = (unsigned *) h_args->mem;
        sm_queue_t *q = (sm_queue_t *) &mem[h_args->queue_ptr];
        unsigned nprio = th[current_context]->nprio;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start_time);

        // Is task queue empty?
        if (!sm_queue_empty(q)) {
            vruntime_scale[current_context] = 1; // Reset penalty
            // Read descriptor from tail
            unsigned descr_offset = sm_queue_can_pop(q);
            gemm_queue_entry_t *e = (gemm_queue_entry_t *) &mem[descr_offset];
            gemm_params_t *params = &(e->gemm_params);
            gemm_access_desc[current_context]->dim_m = params->dim_m;
            gemm_access_desc[current_context]->dim_n = params->dim_n;
            gemm_access_desc[current_context]->dim_k = params->dim_k;
            gemm_access_desc[current_context]->weight_base = params->weight_base;
            gemm_access_desc[current_context]->input_base = params->input_base;
            gemm_access_desc[current_context]->output_base = params->output_base;

            // Wait for output queue to be not full
            sm_queue_t *output_queue = (sm_queue_t *) &(mem[e->common.output_queue]);
            uint64_t output_entry = e->common.output_entry;
            while(sm_queue_full(output_queue)) { SCHED_YIELD; continue; }
            sm_queue_pop(q);
            HIGH_DEBUG(printf("[INVOKE] Starting GEMM %d for context %d on %s\n", invoke_count[current_context], current_context, accel->devname);)

            struct esp_access *esp_access_desc = (struct esp_access *) gemm_access_desc[current_context];
            if (ioctl(accel->fd, GEMM_STRATUS_IOC_ACCESS, esp_access_desc)) {
                perror("ioctl");
                exit(EXIT_FAILURE);
            }
            // Push to output queue
            sm_queue_push(output_queue, output_entry);
            uint64_t *mon_extended = (uint64_t *) esp_access_desc->mon_info.util;
            context_runtime[current_context] += mon_extended[0]; // Single context only
            HIGH_DEBUG(printf("[INVOKE] Finished GEMM %d for context %d on %s\n", invoke_count[current_context]++, current_context, accel->devname);)
        } else {
            vruntime_scale[current_context] += 1; // Penalize for idling
        }

        // Increment time-keeping variables
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stop_time);
        uint64_t elapsed_time = nprio * (uint64_t)(stop_time.tv_sec - start_time.tv_sec) * 1000000000ull;
        elapsed_time += nprio * (uint64_t)(stop_time.tv_nsec - start_time.tv_nsec);
        if (vruntime_scale[current_context] > 64) {
            elapsed_time *= (vruntime_scale[current_context] - 64);
        }
        context_vruntime[current_context] += elapsed_time;
        SCHED_YIELD;
    }
#endif

    return NULL;
}

#ifndef ENABLE_VAM
void gemm_init(physical_accel_t *accel, void *mem) {
    struct gemm_stratus_access *gemm_access_desc;
    gemm_access_desc = (struct gemm_stratus_access *) malloc (sizeof(struct gemm_stratus_access));
    enum contig_alloc_policy policy;
    contig_handle_t *handle = lookup_handle(mem, &policy);
    gemm_access_desc->esp.contig = contig_to_khandle(*handle);
    gemm_access_desc->esp.ddr_node = contig_to_most_allocated(*handle);
    gemm_access_desc->esp.alloc_policy = policy;
    gemm_access_desc->esp.run = true;
    gemm_access_desc->esp.src_offset = 0;
    gemm_access_desc->esp.dst_offset = 0;
    gemm_access_desc->esp.coherence = ACC_COH_RECALL;
    gemm_access_desc->esp.start_stop = 0;
    gemm_access_desc->esp.p2p_store = 0;
    gemm_access_desc->esp.p2p_nsrcs = 0;
    gemm_access_desc->esp.ioctl_cm = ESP_IOCTL_ACC_NO_SM;
    accel->esp_access_desc = (struct esp_access *) gemm_access_desc;
    HIGH_DEBUG(printf("[INVOKE] Created gemm access descriptor for %s\n", accel->devname);)
}

void gemm_run(physical_accel_t *accel, gemm_queue_entry_t *e) {
    gemm_params_t *params = &(e->gemm_params);
    struct gemm_stratus_access *gemm_access_desc = (struct gemm_stratus_access *) accel->esp_access_desc;
    gemm_access_desc->dim_m = params->dim_m;
    gemm_access_desc->dim_n = params->dim_n;
    gemm_access_desc->dim_k = params->dim_k;
    gemm_access_desc->weight_base = params->weight_base;
    gemm_access_desc->input_base = params->input_base;
    gemm_access_desc->output_base = params->output_base;

    HIGH_DEBUG(printf("[INVOKE] Starting GEMM on %s\n", accel->devname);)
    struct esp_access *esp_access_desc = (struct esp_access *) gemm_access_desc;
    if (ioctl(accel->fd, GEMM_STRATUS_IOC_ACCESS, esp_access_desc)) {
        perror("ioctl");
        exit(EXIT_FAILURE);
    }
    uint64_t *mon_extended = (uint64_t *) esp_access_desc->mon_info.util;
    accel->context_active_cycles[0] = mon_extended[0]; // Single context only
    HIGH_DEBUG(printf("[INVOKE] Finished GEMM on %s\n", accel->devname);)
}
#endif
