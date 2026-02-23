#ifndef __BITSET_1_H__
#define __BITSET_1_H__

// Custom implementation of bitset
typedef uint8_t bitset_t;            /* uses only the low 1 bit */

/* internal helpers */
#define BITSET1_MASK(i)   ((uint8_t)(1u << (i)))   /* 0 <= i < 1 */
#define BITSET1_CLAMP(x)  ((uint8_t)((x) & 0x01u))

/* element operations: set/reset/flip/test at position i */
#define bitset_set(v,i)     ((v) = BITSET1_CLAMP((v) |  BITSET1_MASK(i)))
#define bitset_reset(v,i)   ((v) = BITSET1_CLAMP((v) & ~BITSET1_MASK(i)))
#define bitset_flip(v,i)    ((v) = BITSET1_CLAMP((v) ^  BITSET1_MASK(i)))
#define bitset_test(v,i)    (((v) >> (i)) & 1u)

/* aggregate predicates: all/any/none */
#define bitset_all(v)       (BITSET1_CLAMP(v) == 0x01u)
#define bitset_any(v)       (BITSET1_CLAMP(v) != 0)
#define bitset_none(v)      (!bitset_any(v))

/* bulk operations like bitset.set() / bitset.reset() with no index */
#define bitset_set_all(v)   ((v) = 0x0Fu)
#define bitset_reset_all(v) ((v) = 0x00u)

/* Macro-only 1-bit popcount (values 0..1) */
#define bitset_count(v) ((unsigned)("\0\1"[(uint8_t)((v) & 0x01u)]))

#endif // __BITSET_1_H__
