#ifndef __VAM_PHYSICAL_ACCEL_H__
#define __VAM_PHYSICAL_ACCEL_H__

#include <stdio.h>
// #include <hpthread.h>
#include <virtuoso/pthread_types.h>
#include <nptl/descr.h>
#include <pthread.h>

// Utilization entry for tracking
typedef struct util_entry {
    float util[MAX_CONTEXTS];
    unsigned id[MAX_CONTEXTS];
    struct util_entry *next;
    #ifdef LITE_REPORT
    unsigned util_epoch_count;
    #endif
} util_entry_t;

struct cpu_invoke_args_t;
typedef struct cpu_invoke_args_t cpu_invoke_args_t;

// Definition of the physical accelerator struct
// -- includes all information and current status of the physical accelerators
// -- in the system. The same struct is reused to track CPU threads as well.
typedef struct physical_accel_t {
    // VAM-relevant variables
    unsigned accel_id; // ID for tracking
    accel_prim_t prim; // operation of the accelerator
    bitmap_t valid_contexts; // Is the context currently allocated?
    uint64_t context_start_cycles[MAX_CONTEXTS]; // Start counter for the context to use for utilization
    uint64_t context_active_cycles[MAX_CONTEXTS]; // Active cycles for the context to use for utilization
    struct pthread *th[MAX_CONTEXTS]; // If allocated, what is the hpthread in the context?
    float context_util[MAX_CONTEXTS]; // Actual utilization of the context
    float effective_util; // Total utilization of the accelerator
    bool init_done; // Flag to identify whether the device was initialized in the past
    struct physical_accel_t *next; // Next node in accel list
#ifdef DO_PER_INVOKE
    pthread_t cpu_thread[MAX_CONTEXTS]; // If mapped toa CPU, this is the pthread ID
    cpu_invoke_args_t *args[MAX_CONTEXTS]; // If invoked by CPU, these are the arguments
#else
    pthread_t cpu_thread; // If mapped toa CPU, this is the pthread ID
    cpu_invoke_args_t *args; // If invoked by CPU, these are the arguments
#endif
    bool cpu_invoke; // Is the accelerator invoked by a CPU thread?
    util_entry_t *util_entry_list; // Utilization entry list
    unsigned accel_lock; // Lock for the accelerator struct

    // ESP-relevant variables
    char devname[384]; // Name of device in file system
	int ioctl_cm; // IOCTL access code
    int fd; // File descriptor of the device, when open
    struct esp_access *esp_access_desc; // Generic pointer to the access struct.
} physical_accel_t;

// Helper functions for debug
// Print function
static inline void physical_accel_dump(physical_accel_t *accel) {
    printf("\t- accel_id = %d\n", accel->accel_id);
    printf("\t- primitive = %s\n", hpthread_get_prim_name(accel->prim));
    printf("\t- valid_contexts = 0x%x\n", accel->valid_contexts);
    printf("\t- thread_id = ");
    for (int i = 0; i < MAX_CONTEXTS; i++)
        if (bitmap_test(accel->valid_contexts, i))
            printf("%d ", accel->th[i]->accel->id);
    printf("\n");
    printf("\t- effective_util = %0.2f\n", accel->effective_util);
    printf("\t- devname = %s\n", accel->devname);
    printf("\n");
}

// Get name of the device
static inline char *physical_accel_get_name(physical_accel_t *accel) {
    return accel->devname;
}

// Invoke arguments for CPU-invoked accelerators
typedef struct cpu_invoke_args_t {
#ifdef DO_PER_INVOKE
    unsigned context;
    uint64_t active_cycles;
#else
    bitmap_t valid_contexts_ack;
    uint64_t active_cycles[MAX_CONTEXTS];
#endif
    bool kill_pthread;
    physical_accel_t *accel;
} cpu_invoke_args_t;
#endif // __VAM_PHYSICAL_ACCEL_H__
