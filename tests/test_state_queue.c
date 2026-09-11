/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdio.h>
#include "../src/reliability/state_queue.h"

int main(void) {
    uint8_t storage[4], latest = 0, value;
    struct nocfree_state_queue q = {
        .storage = storage, .latest = &latest, .size = 1, .capacity = 4,
    };
    uint8_t press = 1, release = 0;
    nocfree_state_put(&q, &press, 0);
    nocfree_state_put(&q, &release, 1);
    uint32_t ticket = nocfree_state_peek(&q, &value, 2);
    assert(value == 1 && q.count == 2);
    /* Failed sends do not remove the press; successful retries preserve order. */
    assert(nocfree_state_peek(&q, &value, 10) == ticket && value == 1);
    nocfree_state_sent(&q, ticket);
    ticket = nocfree_state_peek(&q, &value, 11);
    assert(value == 0);
    nocfree_state_sent(&q, ticket);
    assert(q.count == 0);
    /* An idle heartbeat repairs the last release even with no further input. */
    ticket = nocfree_state_peek(&q, &value, 111);
    assert(value == 0);
    nocfree_state_sent(&q, ticket);

    nocfree_state_put(&q, &press, 200);
    ticket = nocfree_state_peek(&q, &value, 201);
    for (int i = 0; i < 3; i++) {
        nocfree_state_put(&q, &press, 202);
    }
    nocfree_state_put(&q, &release, 203);
    assert(q.count == 1);
    /* A successful but superseded in-flight send cannot remove the final release. */
    nocfree_state_sent(&q, ticket);
    assert(q.count == 1);
    ticket = nocfree_state_peek(&q, &value, 204);
    assert(value == 0);
    nocfree_state_sent(&q, ticket);

    nocfree_state_put(&q, &press, 300);
    nocfree_state_put(&q, &release, 301);
    ticket = nocfree_state_peek(&q, &value, 550);
    assert(value == 0 && q.count == 1);
    nocfree_state_sent(&q, ticket);
    /* Long randomized overflow/retry runs must always converge to the latest state. */
    unsigned random = 42;
    for (int n = 0; n < 100000; n++) {
        random = random * 1664525U + 1013904223U;
        uint8_t next = random >> 24;
        nocfree_state_put(&q, &next, n + 1000);
        if (random & 1) {
            ticket = nocfree_state_peek(&q, &value, n + 1000);
            if (random & 2) {
                nocfree_state_sent(&q, ticket);
            }
        }
        assert(q.count <= q.capacity);
    }
    ticket = nocfree_state_peek(&q, &value, 200000);
    assert(value == latest);
    nocfree_state_sent(&q, ticket);
    puts("state queue: retries, idle repair, overflow, in-flight race, expiry, stress passed");
}
