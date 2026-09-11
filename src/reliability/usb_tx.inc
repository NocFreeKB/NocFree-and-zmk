/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include "state_queue.h"
#include <zmk/endpoints.h>

struct nocfree_usb_frame {
    uint8_t len;
    uint8_t data[MAX(sizeof(struct zmk_hid_keyboard_report),
                     sizeof(struct zmk_hid_consumer_report))];
};
struct nocfree_usb_stream {
    struct nocfree_state_queue queue;
    struct nocfree_usb_frame latest;
    struct nocfree_usb_frame storage[32];
};
static struct nocfree_usb_stream nocfree_usb_streams[2];
static void nocfree_usb_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(nocfree_usb_work, nocfree_usb_work_cb);

static void nocfree_usb_sync_reset(void) {
    if (atomic_set(&nocfree_usb_reset_pending, 0)) {
        for (int i = 0; i < 2; i++) {
            struct nocfree_state_queue *q = &nocfree_usb_streams[i].queue;
            if (q->capacity) {
                nocfree_state_collapse(q, k_uptime_get());
            }
        }
    }
}

/* Producers and this worker share the system queue, so no transport call holds
 * a lock. Keep the USB endpoint nonblocking: Bluetooth events use this queue. */
static void nocfree_usb_work_cb(struct k_work *work) {
    nocfree_usb_sync_reset();
    bool pending = false;
    bool selected = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
    for (int i = 0; i < 2; i++) {
        struct nocfree_state_queue *q = &nocfree_usb_streams[i].queue;
        if (!q->capacity) {
            continue;
        }
        if (!selected) {
            struct nocfree_usb_frame *latest = &nocfree_usb_streams[i].latest;
            uint8_t report_id = latest->data[0];
            memset(latest->data, 0, sizeof(latest->data));
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
            if (hid_protocol != HID_PROTOCOL_BOOT)
#endif
            {
                latest->data[0] = report_id;
            }
            nocfree_state_collapse(q, k_uptime_get());
            if (latest->len) {
                zmk_usb_hid_send_report(latest->data, latest->len);
            }
            continue;
        }
        if (!nocfree_usb_streams[i].latest.len) {
            continue;
        }
        struct nocfree_usb_frame frame;
        uint32_t ticket = nocfree_state_peek(q, &frame, k_uptime_get());
        int err = zmk_usb_hid_send_report(frame.data, frame.len);
        if (!err) {
            nocfree_state_sent(q, ticket);
        }
        pending |= q->count != 0;
    }
    int delay = pending && zmk_usb_get_status() != USB_DC_SUSPEND ? 1 : 100;
    k_work_schedule(&nocfree_usb_work, K_MSEC(delay));
}

static int nocfree_usb_put(int index, const void *report, size_t len) {
    nocfree_usb_sync_reset();
    struct nocfree_usb_stream *s = &nocfree_usb_streams[index];
    if (len > sizeof(s->latest.data)) {
        return -EINVAL;
    }
    if (!s->queue.capacity) {
        s->queue = (struct nocfree_state_queue){
            .storage = (uint8_t *)s->storage, .latest = (uint8_t *)&s->latest,
            .size = sizeof(s->latest), .capacity = ARRAY_SIZE(s->storage),
        };
    }
    struct nocfree_usb_frame frame = {.len = len};
    memcpy(frame.data, report, len);
    nocfree_state_put(&s->queue, &frame, k_uptime_get());
    /* Only user input may request wakeup. Background repair must not wake a sleeper. */
    if (zmk_usb_get_status() == USB_DC_SUSPEND) {
        usb_wakeup_request();
    }
    k_work_reschedule(&nocfree_usb_work, K_NO_WAIT);
    return 0;
}

int zmk_usb_hid_send_keyboard_report(void) {
    size_t len;
    uint8_t *report = get_keyboard_report(&len);
    return nocfree_usb_put(0, report, len);
}

int zmk_usb_hid_send_consumer_report(void) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol == HID_PROTOCOL_BOOT) {
        return -ENOTSUP;
    }
#endif
    struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
    return nocfree_usb_put(1, report, sizeof(*report));
}
