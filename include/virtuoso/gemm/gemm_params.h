#ifndef __GEMM_PARAMS_H__
#define __GEMM_PARAMS_H__

#define GEMM_PARAM_SIZE 6

// Task parameters for GEMM
typedef struct {
    // Parameters
    unsigned dim_m;
    unsigned dim_n;
    unsigned dim_k;
    unsigned weight_base;
    unsigned input_base;
    unsigned output_base;
} gemm_params_t;

#endif // __GEMM_PARAMS_H__
