/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Caller serializes access. Never hold that lock across a transport call. */
struct nocfree_state_queue {
    uint8_t *storage;
    uint8_t *latest;
    uint16_t size, capacity, head, count;
    uint32_t ticket;
    int64_t pending_since;
};

static inline void nocfree_state_collapse(struct nocfree_state_queue *q, int64_t now) {
    q->head = 0;
    q->count = 1;
    q->ticket++;
    q->pending_since = now;
    memcpy(q->storage, q->latest, q->size);
}

static inline void nocfree_state_put(struct nocfree_state_queue *q, const void *state,
                                     int64_t now) {
    memcpy(q->latest, state, q->size);
    if (q->count == q->capacity) {
        /* Under overload, repair current state; never replay half a shortcut. */
        nocfree_state_collapse(q, now);
        return;
    }
    if (!q->count) {
        q->pending_since = now;
    }
    unsigned tail = (q->head + q->count) % q->capacity;
    memcpy(q->storage + tail * q->size, state, q->size);
    q->count++;
}

static inline uint32_t nocfree_state_peek(struct nocfree_state_queue *q, void *state,
                                         int64_t now) {
    /* Bound stale typing after a long fade, while retaining the final release. */
    if (!q->count || now - q->pending_since >= 250) {
        nocfree_state_collapse(q, now);
    }
    memcpy(state, q->storage + q->head * q->size, q->size);
    return q->ticket;
}

static inline void nocfree_state_sent(struct nocfree_state_queue *q, uint32_t ticket) {
    /* A producer may have collapsed a full queue while transmission was in flight. */
    if (q->count && ticket == q->ticket) {
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        q->ticket++;
    }
}
