/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include "state_queue.h"
#include <zmk/split/bluetooth/peripheral.h>

static uint8_t nocfree_split_storage[CONFIG_ZMK_SPLIT_BLE_PERIPHERAL_POSITION_QUEUE_SIZE]
                                    [POS_STATE_LEN];
static uint8_t nocfree_split_latest[POS_STATE_LEN];
static struct k_spinlock nocfree_split_lock;
static struct nocfree_state_queue nocfree_split_queue = {
    .storage = (uint8_t *)nocfree_split_storage,
    .latest = nocfree_split_latest,
    .size = POS_STATE_LEN,
    .capacity = ARRAY_SIZE(nocfree_split_storage),
};

static void send_position_state_callback(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(service_position_notify_work, send_position_state_callback);

static void send_position_state_callback(struct k_work *work) {
    uint8_t state[POS_STATE_LEN];
    int delay = 100;
    /* Bounded batch: other service work must also get a turn. */
    for (int n = 0; n < 32; n++) {
        k_spinlock_key_t key = k_spin_lock(&nocfree_split_lock);
        if (!zmk_split_bt_peripheral_is_connected()) {
            nocfree_state_collapse(&nocfree_split_queue, k_uptime_get());
            k_spin_unlock(&nocfree_split_lock, key);
            break;
        }
        uint32_t ticket = nocfree_state_peek(&nocfree_split_queue, state, k_uptime_get());
        k_spin_unlock(&nocfree_split_lock, key);
        int err = bt_gatt_notify(NULL, &split_svc.attrs[1], state, sizeof(state));
        key = k_spin_lock(&nocfree_split_lock);
        if (!err) {
            nocfree_state_sent(&nocfree_split_queue, ticket);
        }
        bool pending = nocfree_split_queue.count != 0;
        k_spin_unlock(&nocfree_split_lock, key);
        if (err || !pending) {
            delay = err ? 10 : 100;
            break;
        }
        delay = 1;
    }
    /* A periodic full bitmap repairs an idle lost release and reconnects held keys. */
    k_work_schedule_for_queue(&service_work_q, &service_position_notify_work, K_MSEC(delay));
}

int send_position_state(void) {
    k_spinlock_key_t key = k_spin_lock(&nocfree_split_lock);
    nocfree_state_put(&nocfree_split_queue, position_state, k_uptime_get());
    k_spin_unlock(&nocfree_split_lock, key);
    k_work_reschedule_for_queue(&service_work_q, &service_position_notify_work, K_NO_WAIT);
    return 0;
}
