#pragma once

#include <stdbool.h>
#include <stddef.h>

#define FRAME_POOL_COUNT 3
typedef enum { FRAME_FREE, FRAME_CAPTURING, FRAME_READY, FRAME_PROCESSING } frame_state_t;
typedef struct {
    frame_state_t state[FRAME_POOL_COUNT];
    int ready;
} frame_pool_t;

/* The caller holds the same critical-section lock in the ISR and task. */
static inline void frame_pool_init(frame_pool_t *pool)
{
    for (int i = 0; i < FRAME_POOL_COUNT; ++i) pool->state[i] = FRAME_FREE;
    pool->ready = -1;
}

static inline int frame_pool_capture(frame_pool_t *pool)
{
    for (int i = 0; i < FRAME_POOL_COUNT; ++i) {
        if (pool->state[i] == FRAME_FREE) {
            pool->state[i] = FRAME_CAPTURING;
            return i;
        }
    }
    return -1; /* Let the CSI driver use its backup buffer and drop this frame. */
}

static inline bool frame_pool_complete(frame_pool_t *pool, int index)
{
    if (index < 0 || index >= FRAME_POOL_COUNT || pool->state[index] != FRAME_CAPTURING) return false;
    if (pool->ready >= 0) pool->state[pool->ready] = FRAME_FREE;
    pool->state[index] = FRAME_READY;
    pool->ready = index;
    return true;
}

static inline int frame_pool_take(frame_pool_t *pool)
{
    int index = pool->ready;
    if (index >= 0) {
        pool->state[index] = FRAME_PROCESSING;
        pool->ready = -1;
    }
    return index;
}

static inline void frame_pool_release(frame_pool_t *pool, int index)
{
    if (index >= 0 && index < FRAME_POOL_COUNT && pool->state[index] == FRAME_PROCESSING) {
        pool->state[index] = FRAME_FREE;
    }
}
