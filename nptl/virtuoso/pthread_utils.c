// #include <asm-generic/errno-base.h>
// #include <virtuoso/pthread_types.h>
// #include <virtuoso/pthread_utils.h>
#include <stdio.h>
// #include <string.h>
// #include <fcntl.h>
// #include <list.h>

// #include <dirent.h>
// #include <fnmatch.h>

#include <virtuoso/pthread_utils.h>

// #include <virtuoso/gemm/gemm_stratus.h>
// #include <virtuoso/gemm/gemm_sm_stratus.h>

// #include <virtuoso/gemm/gemm_def.h>

// Global instance of hpthread interface
hpthread_intf_t intf;

// Helper function for swapping the state of the interface
bool hpthread_intf_swap(uint8_t expected_value, uint8_t new_value) {
    return __atomic_compare_exchange_n(&intf.state, &expected_value, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

// Helper function for testing the state of the interface
uint8_t hpthread_intf_test(void) {
    return __atomic_load_n(&intf.state, __ATOMIC_SEQ_CST);
}

// Helper function for setting the state of the interface
void hpthread_intf_set(uint8_t set_value) {
    __atomic_store_n(&intf.state, set_value, __ATOMIC_SEQ_CST);
}

/* TODO: this file is placed under nptl/ only for temporary convenience due to Makefile. It should be placed back to nptl/virtuoso */

// static LIST_HEAD(accel_list);
// static LIST_HEAD(cand_list);

// int __pthread_probe_accelerators(void) {
//     DIR *dir = opendir("/dev/");
//     if (!dir) {
//         perror("Failed to open directory");
//         return ENOTDIR;
//     }

//     struct dirent **list;
//     int n = scandir("/dev/", &list, NULL, alphasort);
//     if (n < 0) {
//         perror("Failed to scan directory");
//         closedir(dir);
//         return ENOENT;
//     }

//     unsigned int device_id = 0;
//     for (int i = 0; i < n; i++) {
//         struct dirent *entry = list[i];
//         if (fnmatch("*_stratus.*", entry->d_name, FNM_NOESCAPE) != 0) {
//             continue;
//         }
//         struct physical_accel_t *accel_temp = (struct physical_accel_t *)malloc(sizeof(struct physical_accel_t));
//         if (!accel_temp) {
//             perror("Failed to allocate memory for accelerator");
//             return ENOMEM;
//         }

//         struct hpthread_cand_t *cand_temp = (struct hpthread_cand_t *) malloc(sizeof(struct hpthread_cand_t));
//         if (!cand_temp) {
//             perror("Failed to allocate memory for candidate");
//             free(accel_temp);
//             return ENOMEM;
//         }
//         accel_temp->accel_id = device_id++;
//         cand_temp->accel_id = accel_temp->accel_id ;
//         bitmap_reset_all(accel_temp->valid_contexts);
//         for (int i = 0; i < MAX_CONTEXTS; i++) {
//             accel_temp->th[i] = NULL;
//             accel_temp->context_start_cycles[i] = 0;
//             accel_temp->context_active_cycles[i] = 0;
//             accel_temp->context_util[i] = 0.0;
//         }
//         strcpy(accel_temp->devname, entry->d_name);
//         accel_temp->init_done = false;
//         accel_temp->effective_util = 0.0;
//         accel_temp->util_entry_list = NULL;
//         __atomic_store_n(&accel_temp->accel_lock, 0, __ATOMIC_RELEASE);

//         if (fnmatch("gemm_sm*", entry->d_name, FNM_NOESCAPE) == 0){
//             gemm_sm_probe(accel_temp);
//         } else if (fnmatch("gemm*", entry->d_name, FNM_NOESCAPE) == 0) {
//             gemm_probe(accel_temp);
//         } else {
//             printf("[ERROR] Device does not match any supported accelerators.\n");
//         }
//         cand_temp->prim = accel_temp->prim;
//         cand_temp->cpu_invoke = accel_temp->cpu_invoke;

//         char full_path[384];
//         snprintf(full_path, 384, "/dev/%s", entry->d_name);
//         accel_temp->fd = open(full_path, O_RDWR, 0);
//         if (accel_temp->fd < 0) {
//             fprintf(stderr, "Error: cannot open %s", full_path);
//             exit(EXIT_FAILURE);
//         }
//         // Reset the accelerator to be sure
//         if (!accel_temp->cpu_invoke) {
//             struct esp_access *esp_access_desc = (struct esp_access *) accel_temp->esp_access_desc;
//             accel_temp->esp_access_desc->ioctl_cm = ESP_IOCTL_ACC_RESET;
//             if (ioctl(accel_temp->fd, accel_temp->ioctl_cm, esp_access_desc)) {
//                 perror("ioctl");
//                 exit(EXIT_FAILURE);
//             }
//         } else {
//             // No reset required for CPU invoke threads
//             #ifdef DO_PER_INVOKE
//             for (int i = 0; i < MAX_CONTEXTS; i++) {
//                 struct cpu_invoke_args_t *args = (struct cpu_invoke_args_t *) malloc (sizeof(struct cpu_invoke_args_t));
//                 accel_temp->args[i] = args;                    
//             }
//             #else
//             struct cpu_invoke_args_t *args = (struct cpu_invoke_args_t *) malloc (sizeof(struct cpu_invoke_args_t));
//             accel_temp->args = args;
//             #endif
//         }

//         list_add_tail(&accel_temp->node, &accel_list);
//         list_add_tail(&cand_temp->node, &cand_list);
//         free(list[i]);
//     }
//     free(list);
//     closedir(dir);

//     // list_t *node;
//     // list_for_each(node, &accel_list) {
//     //     struct physical_accel_t *accel = list_entry(node, struct physical_accel_t, node);
//     //     printf("Found accelerator: %s\n", accel->devname);
//     // }
//     // list_for_each(node, &cand_list) {
//     //     struct hpthread_cand_t *cand = list_entry(node, struct hpthread_cand_t, node);
//     //     printf("Candidate accelerator ID: %u, Primitive: %d, CPU Invoke: %d\n", cand->accel_id, cand->prim, cand->cpu_invoke);
//     // }
//     return 0;
// }
// /applications/test/04_fcnn_mt_pthread/opt.exe 10 2 models/model_64_2.txt
