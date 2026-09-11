/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ZMK_KEYMAP_LEN 105
#define DT_NODELABEL(n) n
#define STRINGIFY(n) #n
#define DEVICE_DT_NAME(n) STRINGIFY(n)
#define LCTRL 0xe0
#define RGUI 0xe7
struct k_work { int unused; };
#define K_WORK_DELAYABLE_DEFINE(n, cb) struct k_work n
#define K_TIMEOUT_ABS_MS(t) (t)
static int64_t clock_ms;
static int64_t k_uptime_get(void) { return clock_ms; }
static void k_work_reschedule(struct k_work *w, int64_t t) { (void)w; (void)t; }
struct zmk_position_state_changed {
    uint8_t source;
    uint32_t position;
    bool state;
    int64_t timestamp;
};
struct zmk_behavior_binding { const char *behavior_dev; unsigned param1; };
static const struct zmk_behavior_binding bindings[] = {{"kp", LCTRL}, {"kp", 4}, {"mo", 1}};
static const struct zmk_behavior_binding *zmk_keymap_get_layer_binding_at_idx(int l, unsigned p) {
    (void)l;
    return &bindings[p];
}
static struct zmk_position_state_changed output[1024];
static unsigned output_count;
static int zmk_keymap_position_state_changed(uint8_t src, uint32_t pos, bool state, int64_t t) {
    output[output_count++] = (struct zmk_position_state_changed){src, pos, state, t};
    return 0;
}
