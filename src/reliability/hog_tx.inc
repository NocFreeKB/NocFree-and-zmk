/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include "state_queue.h"
#include <zmk/endpoints.h>

struct nocfree_hog_stream {
    struct nocfree_state_queue queue;
    struct k_spinlock lock;
    struct k_work_delayable work;
    uint8_t attr_index;
    int profile;
};

#define NOCFREE_HOG_STREAM(name, type, depth, attr)                                                \
    static uint8_t name##_storage[depth][sizeof(type)];                                            \
    static uint8_t name##_latest[sizeof(type)];                                                    \
    static struct nocfree_hog_stream name = {                                                     \
        .queue = {.storage = (uint8_t *)name##_storage, .latest = name##_latest,             \
                  .size = sizeof(type), .capacity = depth},                                      \
        .attr_index = attr, .profile = -1,                                                        \
    }

NOCFREE_HOG_STREAM(nocfree_keyboard, struct zmk_hid_keyboard_report_body,
                   CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE, 5);
NOCFREE_HOG_STREAM(nocfree_consumer, struct zmk_hid_consumer_report_body,
                   CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE, 9);

static void nocfree_hog_release_inactive(struct bt_conn *conn, void *user_data) {
    struct nocfree_hog_stream *s = user_data;
    int profile = zmk_ble_profile_index(bt_conn_get_dst(conn));
    struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
    if (profile < 0 || (selected.transport == ZMK_TRANSPORT_BLE &&
                        selected.ble.profile_index == profile)) {
        return;
    }
    /* Endpoint switching must release the old host even if the first neutral
     * report was rejected. Never send queued typing to an inactive profile. */
    uint8_t released[MAX(sizeof(struct zmk_hid_keyboard_report_body),
                         sizeof(struct zmk_hid_consumer_report_body))] = {0};
    struct bt_gatt_notify_params params = {
        .attr = &hog_svc.attrs[s->attr_index], .data = released, .len = s->queue.size,
    };
    bt_gatt_notify_cb(conn, &params);
}

static void nocfree_hog_send(struct k_work *work) {
    struct nocfree_hog_stream *s =
        CONTAINER_OF(k_work_delayable_from_work(work), struct nocfree_hog_stream, work);
    uint8_t report[MAX(sizeof(struct zmk_hid_keyboard_report_body),
                       sizeof(struct zmk_hid_consumer_report_body))];
    int delay = 100;
    for (int n = 0; n < 32; n++) {
        struct bt_conn *conn = zmk_ble_active_profile_conn();
        k_spinlock_key_t key = k_spin_lock(&s->lock);
        bool selected = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_BLE;
        if (!conn || !selected || s->profile != zmk_ble_active_profile_index()) {
            /* Never carry a queued shortcut into a different host/session. */
            memset(s->queue.latest, 0, s->queue.size);
            nocfree_state_collapse(&s->queue, k_uptime_get());
            s->profile = zmk_ble_active_profile_index();
            k_spin_unlock(&s->lock, key);
            if (conn) {
                bt_conn_unref(conn);
            }
            break;
        }
        uint32_t ticket = nocfree_state_peek(&s->queue, report, k_uptime_get());
        int profile = s->profile;
        k_spin_unlock(&s->lock, key);
        int err = -ENOTCONN;
        if (zmk_ble_profile_index(bt_conn_get_dst(conn)) == profile) {
            struct bt_gatt_notify_params params = {
                .attr = &hog_svc.attrs[s->attr_index], .data = report, .len = s->queue.size,
            };
            err = bt_gatt_notify_cb(conn, &params);
            if (err == -EPERM) {
                bt_conn_set_security(conn, BT_SECURITY_L2);
            }
        }
        bt_conn_unref(conn);
        key = k_spin_lock(&s->lock);
        if (!err) {
            nocfree_state_sent(&s->queue, ticket);
        }
        bool pending = s->queue.count != 0;
        k_spin_unlock(&s->lock, key);
        if (err || !pending) {
            delay = err ? 10 : 100;
            break;
        }
        delay = 1;
    }
    bt_conn_foreach(BT_CONN_TYPE_LE, nocfree_hog_release_inactive, s);
    k_work_schedule_for_queue(&hog_work_q, &s->work, K_MSEC(delay));
}

static int nocfree_hog_put(struct nocfree_hog_stream *s, const void *report) {
    k_spinlock_key_t key = k_spin_lock(&s->lock);
    int profile = zmk_ble_active_profile_index();
    if (s->profile != profile) {
        s->queue.count = 0;
        s->queue.ticket++;
        s->profile = profile;
    }
    nocfree_state_put(&s->queue, report, k_uptime_get());
    k_spin_unlock(&s->lock, key);
    k_work_reschedule_for_queue(&hog_work_q, &s->work, K_NO_WAIT);
    return 0;
}

int zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report_body *report) {
    return nocfree_hog_put(&nocfree_keyboard, report);
}

int zmk_hog_send_consumer_report(struct zmk_hid_consumer_report_body *report) {
    return nocfree_hog_put(&nocfree_consumer, report);
}
