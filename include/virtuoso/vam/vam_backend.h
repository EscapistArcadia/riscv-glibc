#ifndef __VAM_BACKEND_H__
#define __VAM_BACKEND_H__

#include <nptl/descr.h>
#include <virtuoso/vam/vam_physical_accel.h>

// VAM backend is responsible for map virutal hpthreads to physical accelerators
// (or CPU threads) and tracking utilization of these mappings

#define VAM_SLEEP       250000 // 250ms
// Default scheduling period of AVU
#define AVU_SCHED_PERIOD    0x1000000
// Cooldown timer for migration
#define TH_MOVE_COOLDOWN    78125000 // ~1 second

void vam_probe_accel(void);

// Function to wake up and create a thread of VAM
void vam_wakeup(void);
// Main run method
void *vam_run_backend(void *arg);
// Search for accelerator candidates for the hpthread
void vam_search_accel(struct pthread *th);
// Once accelerator candidate is identified, configure the accelerator
void vam_configure_accel(struct pthread *th, physical_accel_t *accel, unsigned context);
// Launch a CPU thread for invoking the accelerator
void vam_configure_cpu_invoke(struct pthread *th, physical_accel_t *accel, unsigned context);
// Similar as accelerator counterpart; this function launches a pthread.
void vam_configure_cpu(struct pthread *th, physical_accel_t *accel);
// Release the accelerator allocated to the hpthread
void vam_release_accel(struct pthread *th);
// Set a new priority for the accelerator allocated to the hpthread
void vam_setprio_accel(struct pthread *th);
// Insert a new physical accelerator struct or CPU thread
void insert_physical_accel(physical_accel_t *accel);
void insert_hpthread_cand(hpthread_cand_t *cand);
void insert_cpu_thread(physical_accel_t *accel);
// Update utilization metrics for all accelerators
void vam_check_utilization(void);
// Checks whether the load is balanced across all acclerators
float vam_check_load_balance(void);
// Runs the load balancing algorithm across all accelerators
bool vam_load_balance(void);
// Read the current utilization and add to log
void vam_log_utilization(void);
// Print out the utilization metrics for the previous epochs in a pretty format
void vam_print_report(void);

#endif // __VAM_BACKEND_H__
