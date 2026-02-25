#ifndef __SM_QUEUE_H__
#define __SM_QUEUE_H__

// #include <hpthread.h>
#include <stdint.h>
#include <stdbool.h>

#define SM_ENTRY_SIZE 2
#define SM_COMMON_SIZE 6
#define SM_QUEUE_SIZE 4

// Context descriptors status
#define QUEUE_INVALID 0
#define QUEUE_AVAIL 1
#define QUEUE_BUSY 2

typedef struct {
    unsigned output_queue;
    unsigned output_entry;
} sm_queue_entry_t;

typedef struct {
    uint64_t stat;
    uint64_t head;
    uint64_t tail;
    uint64_t entry[SM_QUEUE_SIZE];
} sm_queue_t;

static inline void sm_queue_init(sm_queue_t *q) {
    __atomic_store_n(&(q->stat), QUEUE_AVAIL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&(q->head), 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&(q->tail), 0, __ATOMIC_SEQ_CST);
    for (unsigned i = 0; i < SM_QUEUE_SIZE; i++) {
        q->entry[i] = 0;
    }
}

static inline void sm_queue_push(sm_queue_t *q, uint64_t value) {
    uint64_t head = __atomic_load_n(&(q->head), __ATOMIC_ACQUIRE);
    q->entry[head % SM_QUEUE_SIZE] = value;
    __atomic_store_n(&(q->head), head + 1, __ATOMIC_RELEASE);
}

static inline uint64_t sm_queue_can_pop(sm_queue_t *q) {
    uint64_t tail = __atomic_load_n(&(q->tail), __ATOMIC_ACQUIRE);
    return q->entry[tail % SM_QUEUE_SIZE];
}

static inline uint64_t sm_queue_pop(sm_queue_t *q) {
    uint64_t tail = __atomic_load_n(&(q->tail), __ATOMIC_ACQUIRE);
    uint64_t value = q->entry[tail % SM_QUEUE_SIZE];
    __atomic_store_n(&(q->tail), tail + 1, __ATOMIC_RELEASE);
    return value;
}

static inline bool sm_queue_empty(sm_queue_t *q) {
    uint64_t head = __atomic_load_n(&(q->head), __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&(q->tail), __ATOMIC_ACQUIRE);
    return (head == tail);
}

static inline bool sm_queue_full(sm_queue_t *q) {
    uint64_t head = __atomic_load_n(&(q->head), __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&(q->tail), __ATOMIC_ACQUIRE);
    return (head - tail) >= SM_QUEUE_SIZE;
}

static inline unsigned sm_queue_level(sm_queue_t *q) {
    uint64_t head = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE);
    return (unsigned)(head - tail);
}

#endif // __SM_QUEUE_H__
