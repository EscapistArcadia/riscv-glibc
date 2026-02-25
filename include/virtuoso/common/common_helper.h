#ifndef __COMMON_HELPER_H__
#define __COMMON_HELPER_H__

#include <virtuoso/common/common_defines.h>

//  Number of concurrent contexts possible in a single accelerator
#define MAX_CONTEXTS 4

#if (MAX_CONTEXTS == 1)
#include <virtuoso/common/bitset_1.h>
#elif (MAX_CONTEXTS == 2)
#include <virtuoso/common/bitset_2.h>
#elif (MAX_CONTEXTS == 4)
#include <virtuoso/common/bitset_4.h>
#endif

// Helper function to get cycle counter
static inline uint64_t get_counter(void) {
    uint64_t t;
    asm volatile (
        "li t0, 0;"
        "csrr t0, cycle;"
        "mv %0, t0"
        : "=r" (t)
        :
        : "t0"
    );
    return t;
}

#endif // __COMMON_HELPER_H__
