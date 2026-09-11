/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
int main(void) {
    position_state[0] = 1;
    send_position_state();
    position_state[0] = 0;
    send_position_state();
    notify_error = -ENOMEM;
    send_position_state_callback(NULL);
    assert(sent_count == 0 && nocfree_split_queue.count == 2);
    assert(service_position_notify_work.delay == 10);
    notify_error = 0;
    send_position_state_callback(NULL);
    assert(sent_count == 2 && sent_values[0] == 1 && sent_values[1] == 0);
    clock_ms = 100;
    send_position_state_callback(NULL);
    assert(sent_count == 3 && sent_values[2] == 0);
    connected = false;
    position_state[0] = 2;
    send_position_state();
    send_position_state_callback(NULL);
    assert(sent_count == 3);
    connected = true;
    send_position_state_callback(NULL);
    assert(sent_values[3] == 2); /* Held key resynchronizes without another edge. */

    sent_count = 0;
    struct zmk_hid_keyboard_report_body press = {.keys = {1}}, release = {0};
    zmk_hog_send_keyboard_report(&press);
    zmk_hog_send_keyboard_report(&release);
    notify_error = -EPERM;
    nocfree_hog_send(&nocfree_keyboard.work.work);
    assert(sent_count == 0 && security_requests == 1 && conn_refs == 0);
    assert(nocfree_keyboard.queue.count == 2);
    notify_error = 0;
    nocfree_hog_send(&nocfree_keyboard.work.work);
    assert(sent_count == 2 && sent_values[0] == 1 && sent_values[1] == 0);
    zmk_hog_send_keyboard_report(&press);
    active_profile = conn_profile = 1;
    nocfree_hog_send(&nocfree_keyboard.work.work);
    nocfree_hog_send(&nocfree_keyboard.work.work);
    assert(sent_count == 3 && sent_values[2] == 0 && conn_refs == 0);
    zmk_hog_send_keyboard_report(&press);
    connected = false;
    nocfree_hog_send(&nocfree_keyboard.work.work);
    connected = true;
    nocfree_hog_send(&nocfree_keyboard.work.work);
    assert(sent_values[sent_count - 1] == 0); /* No stale shortcut after host reconnect. */

    transport = ZMK_TRANSPORT_USB;
    sent_count = 0;
    keyboard_report.body.keys[0] = 1;
    zmk_usb_hid_send_keyboard_report();
    keyboard_report.body.keys[0] = 0;
    zmk_usb_hid_send_keyboard_report();
    usb_error = 1;
    nocfree_usb_work_cb(NULL);
    assert(sent_count == 0 && nocfree_usb_streams[0].queue.count == 2);
    usb_error = 0;
    nocfree_usb_work_cb(NULL);
    nocfree_usb_work_cb(NULL);
    assert(sent_count == 2 && sent_values[0] == 1 && sent_values[1] == 0);
    usb_status = USB_DC_SUSPEND;
    zmk_usb_hid_send_keyboard_report();
    assert(wake_requests == 1);
    nocfree_usb_work_cb(NULL);
    assert(wake_requests == 1 && sent_count == 2);
    usb_status = 0;
    nocfree_usb_work_cb(NULL);
    assert(sent_count == 3 && sent_values[2] == 0);
    keyboard_report.body.keys[0] = 1;
    zmk_usb_hid_send_keyboard_report();
    keyboard_report.body.keys[0] = 0;
    zmk_usb_hid_send_keyboard_report();
    nocfree_usb_reset_pending = 1;
    nocfree_usb_work_cb(NULL);
    assert(sent_count == 4 && sent_values[3] == 0); /* Reset discards obsolete taps. */
    transport = ZMK_TRANSPORT_BLE;
    nocfree_usb_work_cb(NULL);
    assert(sent_values[sent_count - 1] == 0); /* Clear the old USB host. */
    transport = ZMK_TRANSPORT_USB;
    nocfree_hog_send(&nocfree_keyboard.work.work);
    assert(sent_values[sent_count - 1] == 0); /* Clear the old BLE host. */
    puts("transports: retry, repair, reconnect, profile isolation, USB busy/suspend passed");
}
