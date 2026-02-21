#ifndef _ACCELTYPES_H_VIRTUOSO
#define _ACCELTYPES_H_VIRTUOSO

#include <internaltypes.h>
#include <stdint.h>

/**
 * @todo To ensure the generality, we may want to make PRIM dynamic and support
 * dynamic registration of primitives. For now, we just port specific primitives
 * we care about.
 */
typedef uint8_t accel_primitive_t;

#define PRIM_NONE 0
#define PRIM_GEMM 4

#define primitive_is_valid(prim) ((prim) >= 0)

struct pthread_accel_attr_t {
    accel_primitive_t prim;
    void *mem;
    uint64_t queue_ptr;
};

#endif
