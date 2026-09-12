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
#include "../drivers/kscan/kscan_pca9555_scan.h"
#define CONFIG_NOCFREE_KSCAN_PCA9555_STACK_SIZE 1024
#define CONFIG_NOCFREE_KSCAN_PCA9555_PRIORITY 4
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...)
#define LOG_WRN(...)
#define LOG_DBG(...)
#define SYS_INIT(...)
#define K_THREAD_STACK_DEFINE(name, size) char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
#define K_TIMEOUT_ABS_MS(t) (t)
#define K_NO_WAIT 0
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CLAMP(v,a,b) MIN(MAX(v,a),b)
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define DEBOUNCE_COUNTER_MAX 16383
struct device { void *data; const void *config; };
typedef void (*kscan_callback_t)(const struct device *, uint32_t, uint32_t, bool);
struct kscan_driver_api {
    int (*config)(const struct device *, kscan_callback_t);
    int (*enable_callback)(const struct device *);
    int (*disable_callback)(const struct device *);
};
struct k_work { int unused; };
struct k_work_delayable { struct k_work work; };
struct k_work_q { int unused; };
struct k_work_queue_config { const char *name; };
static int64_t clock_ms, next_scan;
static int64_t k_uptime_get(void) { return clock_ms; }
static void k_work_queue_start(struct k_work_q *q, void *s, size_t n, int p,
                               const struct k_work_queue_config *c) {}
static void k_work_init_delayable(struct k_work_delayable *w, void (*cb)(struct k_work *)) {}
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *w) {
    return CONTAINER_OF(w, struct k_work_delayable, work);
}
static void k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *w,
                                        int64_t t) { next_scan = t; }
static void k_work_cancel_delayable(struct k_work_delayable *w) {}
struct i2c_dt_spec { unsigned addr; };
static bool bus_failed, bad_readback;
static int fail_address = -1;
static uint16_t inputs[2] = {0xffff, 0xffff};
static bool i2c_is_ready_dt(const struct i2c_dt_spec *d) { return true; }
static int i2c_write_dt(const struct i2c_dt_spec *d, void *b, size_t n) {
    return bus_failed ? -EIO : 0;
}
static int i2c_write_read_dt(const struct i2c_dt_spec *d, const void *cmd, size_t cn,
                             void *buf, size_t bn) {
    if (bus_failed || (int)d->addr == fail_address) { return -EIO; }
    unsigned reg = *(const uint8_t *)cmd;
    uint16_t value = reg == 0 ? inputs[d->addr] : reg == 6 ? 0xffff : 0;
    if (bad_readback && reg == 4) { value = 0xffff; }
    nocfree_pca9555_port_bytes(value, buf);
    return 0;
}
struct zmk_debounce_state { bool pressed, changed; uint16_t counter; };
struct zmk_debounce_config { uint32_t debounce_press_ms, debounce_release_ms; };
/* Test only outage state handling; successful decoding is covered separately.
 * Zero thresholds make the debouncer deterministic for these fault scenarios. */
static void zmk_debounce_update(struct zmk_debounce_state *s, bool active, int elapsed,
                                const struct zmk_debounce_config *c) {
    assert(c->debounce_press_ms == 0 && c->debounce_release_ms == 0);
    s->changed = s->pressed != active;
    s->pressed = active;
}
static bool zmk_debounce_get_changed(struct zmk_debounce_state *s) { return s->changed; }
static bool zmk_debounce_is_pressed(struct zmk_debounce_state *s) { return s->pressed; }
static bool zmk_debounce_is_active(struct zmk_debounce_state *s) { return s->pressed; }
