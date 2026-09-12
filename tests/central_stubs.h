/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include "transport_stubs.h"
#define POSITION_STATE_DATA_LEN 16
#define ZMK_SPLIT_BLE_PERIPHERAL_COUNT 2
#define CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS 2
#define ZMK_KEYMAP_LEN 105
#define BUILD_ASSERT(c) _Static_assert(c, #c)
#define BIT(n) (1U << (n))
#define BT_GATT_ITER_CONTINUE 1
#define BT_GATT_ITER_STOP 0
#define BT_LE_SCAN_PASSIVE NULL
#define K_WORK_DEFINE(name, cb) struct k_work name
#define LOG_DBG(...)
#define LOG_ERR(...)
#define LOG_WRN(...)
#define ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_KEY_POSITION_EVENT 1
static bool is_scanning, is_enabled = true;
static int scan_error, scan_calls;
static int bt_le_scan_start(void *params, void *cb) { scan_calls++; return scan_error; }
static void *split_central_device_found;
static struct peripheral_slot {
    struct bt_conn *conn;
    int last_state_at;
} peripherals[2];
static struct peripheral_slot *peripheral_slot_for_conn(struct bt_conn *conn) {
    return conn == &connection ? &peripherals[0] : NULL;
}
static int peripheral_slot_index_for_conn(struct bt_conn *conn) { return 0; }
static int k_uptime_get_32(void) { return clock_ms; }
static void k_work_submit(struct k_work *w) {}
struct bt_gatt_subscribe_params { int value_handle; };
struct zmk_split_transport_peripheral_event {
    int type;
    struct { struct { int position; bool pressed; } key_position_event; } data;
};
static int bt_central, position_events;
static bool held[2][105];
static void zmk_split_transport_central_peripheral_event_handler(void *transport, int index,
                                      struct zmk_split_transport_peripheral_event ev) {
    held[index][ev.data.key_position_event.position] = ev.data.key_position_event.pressed;
    position_events++;
}
