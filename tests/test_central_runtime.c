/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
int main(void) {
    scan_error = -ENOMEM;
    assert(start_scanning() == -ENOMEM && !is_scanning && scan_calls == 1);
    scan_error = 0;
    assert(start_scanning() == 0 && is_scanning && scan_calls == 2);
    assert(start_scanning() == 0 && scan_calls == 2);
    is_scanning = false;
    is_enabled = false;
    assert(start_scanning() == 0 && scan_calls == 2);
    is_enabled = true;
    scan_error = -EALREADY;
    assert(start_scanning() == 0 && is_scanning);

    struct bt_gatt_subscribe_params sub = {1};
    uint8_t state[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    /* A short packet must not read beyond its payload or alter any state. */
    assert(split_central_notify_func(&connection, &sub, state, 1) == BT_GATT_ITER_CONTINUE);
    nocfree_rx_work_cb(NULL);
    assert(position_events == 0);
    split_central_notify_func(&connection, &sub, state, 16);
    nocfree_rx_work_cb(NULL);
    assert(position_events == 48);
    memset(state, 0, sizeof(state));
    nocfree_receive_state(0, state, true);
    nocfree_rx_work_cb(NULL);
    assert(position_events == 96);
    for (int i = 0; i < 48; i++) { assert(!held[0][i]); }
    /* Flood beyond capacity, then stop typing: the final state still wins. */
    for (int n = 0; n < 100; n++) {
        memset(state, (n & 1) ? 0 : 0xff, sizeof(state));
        split_central_notify_func(&connection, &sub, state, 16);
    }
    nocfree_rx_work_cb(NULL);
    for (int i = 0; i < 105; i++) { assert(!held[0][i]); }
    state[0] = 1;
    nocfree_receive_state(1, state, false);
    nocfree_rx_work_cb(NULL);
    assert(held[1][0] && !held[0][0]);
    assert(split_central_notify_func(&connection, &sub, NULL, 0) == BT_GATT_ITER_STOP);
    assert(sub.value_handle == 0);
    puts("central: scan failure/retry, malformed packet, 48 releases, overflow, two slots passed");
}
