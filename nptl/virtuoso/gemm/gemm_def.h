#ifndef __GEMM_DEF_H__
#define __GEMM_DEF_H__

#include <gemm_queue.h>

// Wrapper for GEMM to be mapped for the hpthread
void *sw_gemm(void *a);

// Device-dependent probe function for baseline accelerator
void gemm_probe(physical_accel_t *accel);

// Device-dependent probe function for SM accelerator
void gemm_sm_probe(physical_accel_t *accel);

// GEMM accelerator invocation thread
void *gemm_invoke(void *a);

#ifndef ENABLE_VAM
bool allocate_gemm_accel(unsigned accel_idx);

void gemm_init(physical_accel_t *accel, void *mem);

void gemm_run(physical_accel_t *accel, gemm_queue_entry_t *e);
#endif

#endif // __GEMM_DEF_H__
