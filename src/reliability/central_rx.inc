/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include "state_queue.h"
#include <zmk/matrix.h>
BUILD_ASSERT(ZMK_KEYMAP_LEN <= POSITION_STATE_DATA_LEN * 8);

/* Queue complete snapshots, so overload cannot lose just one release edge. */
static uint8_t nocfree_rx_storage[ZMK_SPLIT_BLE_PERIPHERAL_COUNT][32][POSITION_STATE_DATA_LEN];
static uint8_t nocfree_rx_latest[ZMK_SPLIT_BLE_PERIPHERAL_COUNT][POSITION_STATE_DATA_LEN];
static uint8_t nocfree_rx_delivered[ZMK_SPLIT_BLE_PERIPHERAL_COUNT][POSITION_STATE_DATA_LEN];
static struct nocfree_state_queue nocfree_rx[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
static struct k_spinlock nocfree_rx_lock;
static void nocfree_rx_work_cb(struct k_work *work);
static K_WORK_DEFINE(nocfree_rx_work, nocfree_rx_work_cb);

static void nocfree_receive_state(int index, const uint8_t *state, bool disconnected) {
    k_spinlock_key_t key = k_spin_lock(&nocfree_rx_lock);
    struct nocfree_state_queue *q = &nocfree_rx[index];
    if (!q->capacity) {
        q->storage = (uint8_t *)nocfree_rx_storage[index];
        q->latest = nocfree_rx_latest[index];
        q->size = POSITION_STATE_DATA_LEN;
        q->capacity = 32;
    }
    nocfree_state_put(q, state, k_uptime_get());
    if (disconnected) {
        nocfree_state_collapse(q, k_uptime_get());
    }
    k_spin_unlock(&nocfree_rx_lock, key);
    k_work_submit(&nocfree_rx_work);
}
