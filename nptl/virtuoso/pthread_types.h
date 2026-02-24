#ifndef __VIRTUOSO_ACCEL_TYPES_H__
#define __VIRTUOSO_ACCEL_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <list.h>

#define ATTR_FLAG_ACCELERATOR 0x0080
#define primitive_is_valid(prim) ((prim) >= PRIM_NONE && (prim) <= PRIM_MAX)

#define PRIM_NONE 0
#define PRIM_AUDIO_FFT 1
#define PRIM_AUDIO_FIR 2
#define PRIM_AUDIO_FFI 3
#define PRIM_GEMM 4
#define PRIM_MAX PRIM_GEMM

typedef uint8_t accel_prim_t;

struct pthread_accel_attr_t;
struct pthread_accel_t;
struct physical_accel_t;

struct pthread_accel_attr_t {
    accel_prim_t prim;
    void *mem;
    uint64_t queue_ptr;
};

struct pthread_accel_t {
    unsigned int id;
    accel_prim_t prim;
    void *mem;
    uint64_t queue_ptr;
    bool *kill_pthread;
    float th_util;
    struct physical_accel_t *accel;
    unsigned accel_context;
	bool is_active;
    uint64_t th_last_move;
    bool cpu_invoke;
    unsigned affinity;
    // Debug variables
    char name[100];
    unsigned user_id;
};

/* TODO: I will move the following definitions to another header. */
/* TODO: DO_PER_INVOKE */

//  Number of concurrent contexts possible in a single accelerator
#define MAX_CONTEXTS 4

#if (MAX_CONTEXTS == 1)
#include <virtuoso/bitset/bitset_1.h>
#elif (MAX_CONTEXTS == 2)
#include <virtuoso/bitset/bitset_2.h>
#elif (MAX_CONTEXTS == 4)
#include <virtuoso/bitset/bitset_4.h>
#endif

// Invoke arguments for CPU-invoked accelerators
struct cpu_invoke_args_t {
#ifdef DO_PER_INVOKE
    unsigned context;
    uint64_t active_cycles;
#else
    bitset_t valid_contexts_ack;
    uint64_t active_cycles[MAX_CONTEXTS];
#endif
    bool kill_pthread;
    struct physical_accel_t *accel;
};

typedef struct util_entry {
    float util[MAX_CONTEXTS];
    unsigned id[MAX_CONTEXTS];
    struct util_entry *next;
    #ifdef LITE_REPORT
    unsigned util_epoch_count;
    #endif
} util_entry_t;

struct physical_accel_t {
    /* device metadata */
    unsigned accel_id; // ID for tracking
    accel_prim_t prim; // operation of the accelerator
    bool cpu_invoke; // Is the accelerator invoked by a CPU thread?

    /* hardware scheduling information */
    bitset_t valid_contexts; // Is the context currently allocated?
    uint64_t context_start_cycles[MAX_CONTEXTS]; // Start counter for the context to use for utilization
    uint64_t context_active_cycles[MAX_CONTEXTS]; // Active cycles for the context to use for utilization
    struct pthread *th[MAX_CONTEXTS]; // If allocated, what is the hpthread in the context?
    float context_util[MAX_CONTEXTS]; // Actual utilization of the context
    float effective_util; // Total utilization of the accelerator
    util_entry_t *util_entry_list; // Utilization entry list

    /* device status information */
    bool init_done; // Flag to identify whether the device was initialized in the past
#ifdef DO_PER_INVOKE
    pthread_t cpu_thread[MAX_CONTEXTS]; // If mapped toa CPU, this is the pthread ID
    struct cpu_invoke_args_t *args[MAX_CONTEXTS]; // If invoked by CPU, these are the arguments
#else
    pthread_t cpu_thread; // If mapped toa CPU, this is the pthread ID
    struct cpu_invoke_args_t *args; // If invoked by CPU, these are the arguments
#endif
    unsigned accel_lock; // Lock for the accelerator struct

    /* ESP-relevant variables */
    char devname[384]; // Name of device in file system
	int ioctl_cm; // IOCTL access code
    int fd; // File descriptor of the device, when open
    struct esp_access *esp_access_desc; // Generic pointer to the access struct.

    /* Linked list pointers */    
    // struct physical_accel_t *next; // Next node in accel list
    list_t node;
};

struct hpthread_cand_t {
    unsigned accel_id;
    accel_prim_t prim;
    bool cpu_invoke;
    // struct hpthread_cand_t *next;
    list_t node;
};

#endif
