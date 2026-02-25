#ifndef __COMMON_DEFS_H__
#define __COMMON_DEFS_H__

// #include <stdint.h>
// #include <stdio.h>
// #include <stdbool.h>
// #include <time.h>

// AVU synchronization variable size and offsets (in bytes)
#define VALID_OFFSET 0
#define PAYLOAD_OFFSET 8

// Context descriptors status
#define QUEUE_INVALID 0
#define QUEUE_AVAIL 1
#define QUEUE_BUSY 2

// Different debug levels for prints
#if defined(HIGH_VERBOSE)
#define HIGH_DEBUG(x) x
#define LOW_DEBUG(x) x
#elif defined(LOW_VERBOSE)
#define HIGH_DEBUG(x)
#define LOW_DEBUG(x) x
#else
#define HIGH_DEBUG(x)
#define LOW_DEBUG(x)
#endif

#ifndef NO_YIELD
#define SCHED_YIELD sched_yield()
#else
#define SCHED_YIELD
#endif

// Look up table for nice values based on priority (0-4)
// 5 is for idle low priority threads
static const int nice_table[6] = {
    -6, -3, -2, -1, 0, 10
};

#endif // __COMMON_DEFS_H__
