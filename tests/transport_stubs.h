/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define K_MSEC(t) (t)
#define K_NO_WAIT 0
#define IS_ENABLED(x) 0
#define POS_STATE_LEN 16
#define CONFIG_ZMK_SPLIT_BLE_PERIPHERAL_POSITION_QUEUE_SIZE 32
#define CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE 32
#define CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE 16
#define BT_SECURITY_L2 2
#define BT_CONN_TYPE_LE 1
#define ZMK_TRANSPORT_BLE 1
#define ZMK_TRANSPORT_USB 2
#define USB_DC_SUSPEND 3
struct k_work { int unused; };
struct k_work_delayable { struct k_work work; int delay; };
struct k_work_q { int unused; };
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
#define K_WORK_DELAYABLE_DEFINE(name, cb) struct k_work_delayable name
static int64_t clock_ms;
static int64_t k_uptime_get(void) { return clock_ms; }
static int k_spin_lock(struct k_spinlock *l) { return 0; }
static void k_spin_unlock(struct k_spinlock *l, int k) {}
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *w) {
    return CONTAINER_OF(w, struct k_work_delayable, work);
}
static void k_work_schedule_for_queue(struct k_work_q *q, struct k_work_delayable *w, int t) {
    w->delay = t;
}
static void k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *w, int t) {
    w->delay = t;
}
static void k_work_schedule(struct k_work_delayable *w, int t) { w->delay = t; }
static void k_work_reschedule(struct k_work_delayable *w, int t) { w->delay = t; }
static struct k_work_q service_work_q, hog_work_q;
static struct { int attrs[16]; } split_svc, hog_svc;
static uint8_t position_state[16];
static bool connected = true;
static bool zmk_split_bt_peripheral_is_connected(void) { return connected; }
static int notify_error, sent_count, sent_values[1024], active_profile, transport = 1;
static int conn_profile, conn_refs, security_requests;
struct bt_conn { int unused; };
static struct bt_conn connection;
struct bt_gatt_notify_params { void *attr; void *data; size_t len; };
static int bt_gatt_notify(void *conn, void *attr, const void *data, size_t len) {
    if (notify_error) { return notify_error; }
    sent_values[sent_count++] = *(const uint8_t *)data;
    return 0;
}
static int bt_gatt_notify_cb(struct bt_conn *conn, struct bt_gatt_notify_params *p) {
    return bt_gatt_notify(conn, p->attr, p->data, p->len);
}
static struct bt_conn *zmk_ble_active_profile_conn(void) {
    if (!connected) { return NULL; }
    conn_refs++;
    return &connection;
}
static void bt_conn_unref(struct bt_conn *conn) { conn_refs--; }
static int zmk_ble_active_profile_index(void) { return active_profile; }
static const void *bt_conn_get_dst(struct bt_conn *c) { return c; }
static int zmk_ble_profile_index(const void *addr) { return conn_profile; }
static void bt_conn_set_security(struct bt_conn *c, int level) { security_requests++; }
struct zmk_endpoint_instance { int transport; struct { int profile_index; } ble; };
static struct zmk_endpoint_instance zmk_endpoint_get_selected(void) {
    return (struct zmk_endpoint_instance){transport, {active_profile}};
}
static void bt_conn_foreach(int type, void (*cb)(struct bt_conn *, void *), void *p) {
    if (connected) { cb(&connection, p); }
}
struct zmk_hid_keyboard_report_body { uint8_t keys[8]; };
struct zmk_hid_consumer_report_body { uint8_t keys[2]; };
struct zmk_hid_keyboard_report { uint8_t id; struct zmk_hid_keyboard_report_body body; };
struct zmk_hid_consumer_report { uint8_t id; struct zmk_hid_consumer_report_body body; };
static struct zmk_hid_keyboard_report keyboard_report;
static struct zmk_hid_consumer_report consumer_report;
static int usb_status, usb_error, wake_requests, nocfree_usb_reset_pending;
static int atomic_set(int *p, int value) { int old = *p; *p = value; return old; }
static uint8_t *get_keyboard_report(size_t *len) {
    *len = sizeof(keyboard_report);
    return (uint8_t *)&keyboard_report;
}
static struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) {
    return &consumer_report;
}
static int zmk_usb_get_status(void) { return usb_status; }
static int usb_wakeup_request(void) { wake_requests++; return 0; }
static int zmk_usb_hid_send_report(const void *report, size_t len) {
    if (usb_error || usb_status == USB_DC_SUSPEND) { return -EAGAIN; }
    sent_values[sent_count++] = ((const uint8_t *)report)[1];
    return 0;
}
