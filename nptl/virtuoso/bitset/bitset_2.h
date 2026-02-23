#ifndef __BITSET_2_H__
#define __BITSET_2_H__

// Custom implementation of bitset
typedef uint8_t bitset_t;            /* uses only the low 2 bits */

/* internal helpers */
#define BITSET2_MASK(i)   ((uint8_t)(1u << (i)))   /* 0 <= i < 2 */
#define BITSET2_CLAMP(x)  ((uint8_t)((x) & 0x03u))

/* element operations: set/reset/flip/test at position i */
#define bitset_set(v,i)     ((v) = BITSET2_CLAMP((v) |  BITSET2_MASK(i)))
#define bitset_reset(v,i)   ((v) = BITSET2_CLAMP((v) & ~BITSET2_MASK(i)))
#define bitset_flip(v,i)    ((v) = BITSET2_CLAMP((v) ^  BITSET2_MASK(i)))
#define bitset_test(v,i)    (((v) >> (i)) & 1u)

/* aggregate predicates: all/any/none */
#define bitset_all(v)       (BITSET2_CLAMP(v) == 0x03u)
#define bitset_any(v)       (BITSET2_CLAMP(v) != 0)
#define bitset_none(v)      (!bitset_any(v))

/* bulk operations like bitset.set() / bitset.reset() with no index */
#define bitset_set_all(v)   ((v) = 0x03u)
#define bitset_reset_all(v) ((v) = 0x00u)

/* Macro-only 2-bit popcount (values 0..3) */
#define bitset_count(v) ((unsigned)("\0\1\1\2"[(uint8_t)((v) & 0x03u)]))

#endif // __BITSET_2_H__
