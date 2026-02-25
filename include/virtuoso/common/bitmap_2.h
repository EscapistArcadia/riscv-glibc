#ifndef __BITMAP_2_H__
#define __BITMAP_2_H__

// Custom implementation of bitmap
typedef uint8_t bitmap_t;            /* uses only the low 2 bits */

/* internal helpers */
#define BITMAP2_MASK(i)   ((uint8_t)(1u << (i)))   /* 0 <= i < 2 */
#define BITMAP2_CLAMP(x)  ((uint8_t)((x) & 0x03u))

/* element operations: set/reset/flip/test at position i */
#define bitmap_set(v,i)     ((v) = BITMAP2_CLAMP((v) |  BITMAP2_MASK(i)))
#define bitmap_reset(v,i)   ((v) = BITMAP2_CLAMP((v) & ~BITMAP2_MASK(i)))
#define bitmap_flip(v,i)    ((v) = BITMAP2_CLAMP((v) ^  BITMAP2_MASK(i)))
#define bitmap_test(v,i)    (((v) >> (i)) & 1u)

/* aggregate predicates: all/any/none */
#define bitmap_all(v)       (BITMAP2_CLAMP(v) == 0x03u)
#define bitmap_any(v)       (BITMAP2_CLAMP(v) != 0)
#define bitmap_none(v)      (!bitmap_any(v))

/* bulk operations like bitmap.set() / bitmap.reset() with no index */
#define bitmap_set_all(v)   ((v) = 0x03u)
#define bitmap_reset_all(v) ((v) = 0x00u)

/* Macro-only 2-bit popcount (values 0..3) */
#define bitmap_count(v) ((unsigned)("\0\1\1\2"[(uint8_t)((v) & 0x03u)]))

#endif // __BITMAP_2_H__
