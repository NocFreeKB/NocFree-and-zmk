/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
static void nocfree_rx_work_cb(struct k_work *work) {
    for (int index = 0; index < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; index++) {
        struct nocfree_state_queue *q = &nocfree_rx[index];
        for (int batch = 0; batch < 32; batch++) {
            uint8_t state[POSITION_STATE_DATA_LEN];
            k_spinlock_key_t key = k_spin_lock(&nocfree_rx_lock);
            if (!q->count) {
                k_spin_unlock(&nocfree_rx_lock, key);
                break;
            }
            uint32_t ticket = nocfree_state_peek(q, state, k_uptime_get());
            nocfree_state_sent(q, ticket);
            k_spin_unlock(&nocfree_rx_lock, key);
            for (int position = 0; position < ZMK_KEYMAP_LEN; position++) {
                int byte = position / 8;
                uint8_t bit = BIT(position % 8);
                if (!((state[byte] ^ nocfree_rx_delivered[index][byte]) & bit)) {
                    continue;
                }
                struct zmk_split_transport_peripheral_event ev = {
                    .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_KEY_POSITION_EVENT,
                    .data.key_position_event = {
                        .position = position, .pressed = (state[byte] & bit) != 0,
                    },
                };
                zmk_split_transport_central_peripheral_event_handler(&bt_central, index, ev);
            }
            memcpy(nocfree_rx_delivered[index], state, sizeof(state));
        }
        k_spinlock_key_t key = k_spin_lock(&nocfree_rx_lock);
        bool pending = q->count != 0;
        k_spin_unlock(&nocfree_rx_lock, key);
        if (pending) {
            k_work_submit(&nocfree_rx_work);
        }
    }
}
