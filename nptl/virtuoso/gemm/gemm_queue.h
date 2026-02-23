#ifndef __GEMM_QUEUE_H__
#define __GEMM_QUEUE_H__

#include <sm_queue.h>
#include <gemm_node_args.h>

#define GEMM_ENTRY_SIZE (SM_ENTRY_SIZE + GEMM_PARAM_SIZE)

typedef struct {
    sm_queue_entry_t common;
    gemm_params_t gemm_params;
} gemm_queue_entry_t;

static inline void print_gemm_entry(gemm_queue_entry_t *e) {
    printf("\toutput_queue=%d\n", e->common.output_queue);
    printf("\toutput_entry=%d\n", e->common.output_entry);
    printf("\tdim_m=%d\n", e->gemm_params.dim_m);
    printf("\tdim_n=%d\n", e->gemm_params.dim_n);
    printf("\tdim_k=%d\n", e->gemm_params.dim_k);
    printf("\tweight_base=%d\n", e->gemm_params.weight_base);
    printf("\tinput_base=%d\n", e->gemm_params.input_base);
    printf("\toutput_base=%d\n", e->gemm_params.output_base);
}
    
#endif // __GEMM_QUEUE_H__
