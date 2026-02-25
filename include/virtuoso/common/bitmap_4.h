#ifndef __BITMAP_4_H__
#define __BITMAP_4_H__

// Custom implementation of bitmap
typedef uint8_t bitmap_t;            /* uses only the low 4 bits */

/* internal helpers */
#define BITMAP4_MASK(i)   ((uint8_t)(1u << (i)))   /* 0 <= i < 4 */
#define BITMAP4_CLAMP(x)  ((uint8_t)((x) & 0x0Fu))

/* element operations: set/reset/flip/test at position i */
#define bitmap_set(v,i)     ((v) = BITMAP4_CLAMP((v) |  BITMAP4_MASK(i)))
#define bitmap_reset(v,i)   ((v) = BITMAP4_CLAMP((v) & ~BITMAP4_MASK(i)))
#define bitmap_flip(v,i)    ((v) = BITMAP4_CLAMP((v) ^  BITMAP4_MASK(i)))
#define bitmap_test(v,i)    (((v) >> (i)) & 1u)

/* aggregate predicates: all/any/none */
#define bitmap_all(v)       (BITMAP4_CLAMP(v) == 0x0Fu)
#define bitmap_any(v)       (BITMAP4_CLAMP(v) != 0)
#define bitmap_none(v)      (!bitmap_any(v))

/* bulk operations like bitmap.set() / bitmap.reset() with no index */
#define bitmap_set_all(v)   ((v) = 0x0Fu)
#define bitmap_reset_all(v) ((v) = 0x00u)

/* Macro-only 4-bit popcount (values 0..15) */
#define bitmap_count(v) ((unsigned)("\0\1\1\2\1\2\2\3\1\2\2\3\2\3\3\4"[(uint8_t)((v) & 0x0Fu)]))

#endif // __BITMAP_4_H__
