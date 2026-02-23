#ifndef __GEMM_NODE_ARGS_H__
#define __GEMM_NODE_ARGS_H__

#include <gemm_params.h>

// Argument struct for GEMM
typedef struct {
    gemm_params_t params;
    // Input file for read-only parameters
    char input_file[100];
} gemm_node_args;

#endif // __GEMM_NODE_ARGS_H__
