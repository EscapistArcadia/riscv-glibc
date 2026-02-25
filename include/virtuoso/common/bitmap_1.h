#ifndef __BITMAP_1_H__
#define __BITMAP_1_H__

// Custom implementation of bitmap
typedef uint8_t bitmap_t;            /* uses only the low 1 bit */

/* internal helpers */
#define BITMAP1_MASK(i)   ((uint8_t)(1u << (i)))   /* 0 <= i < 1 */
#define BITMAP1_CLAMP(x)  ((uint8_t)((x) & 0x01u))

/* element operations: set/reset/flip/test at position i */
#define bitmap_set(v,i)     ((v) = BITMAP1_CLAMP((v) |  BITMAP1_MASK(i)))
#define bitmap_reset(v,i)   ((v) = BITMAP1_CLAMP((v) & ~BITMAP1_MASK(i)))
#define bitmap_flip(v,i)    ((v) = BITMAP1_CLAMP((v) ^  BITMAP1_MASK(i)))
#define bitmap_test(v,i)    (((v) >> (i)) & 1u)

/* aggregate predicates: all/any/none */
#define bitmap_all(v)       (BITMAP1_CLAMP(v) == 0x01u)
#define bitmap_any(v)       (BITMAP1_CLAMP(v) != 0)
#define bitmap_none(v)      (!bitmap_any(v))

/* bulk operations like bitset.set() / bitset.reset() with no index */
#define bitmap_set_all(v)   ((v) = 0x0Fu)
#define bitmap_reset_all(v) ((v) = 0x00u)

/* Macro-only 1-bit popcount (values 0..1) */
#define bitmap_count(v) ((unsigned)("\0\1"[(uint8_t)((v) & 0x01u)]))

#endif // __BITMAP_1_H__
