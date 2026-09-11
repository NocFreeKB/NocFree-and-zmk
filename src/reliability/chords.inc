/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#include <dt-bindings/zmk/keys.h>

/* All callers and this work item run on the system queue. Preserve ordinary
 * key order while letting a modifier/Fn press overtake up to 15 ms of radio
 * skew. Releases wait too, so a quick chord cannot release its modifier early.
 * This is intentionally scoped to this board's plain &kp / &mo base map. */
static struct zmk_position_state_changed nocfree_chords[128];
static int64_t nocfree_chord_deadlines[128];
static unsigned nocfree_chord_head, nocfree_chord_count;
static void nocfree_chord_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(nocfree_chord_work, nocfree_chord_work_cb);

static void nocfree_chord_pop(void) {
    struct zmk_position_state_changed ev = nocfree_chords[nocfree_chord_head];
    nocfree_chord_head = (nocfree_chord_head + 1) % ARRAY_SIZE(nocfree_chords);
    nocfree_chord_count--;
    zmk_keymap_position_state_changed(ev.source, ev.position, ev.state, ev.timestamp);
}

static void nocfree_chord_flush_due(void) {
    while (nocfree_chord_count &&
           nocfree_chord_deadlines[nocfree_chord_head] <= k_uptime_get()) {
        nocfree_chord_pop();
    }
}

static void nocfree_chord_work_cb(struct k_work *work) {
    nocfree_chord_flush_due();
    if (nocfree_chord_count) {
        k_work_reschedule(&nocfree_chord_work,
                          K_TIMEOUT_ABS_MS(nocfree_chord_deadlines[nocfree_chord_head]));
    }
}

static int nocfree_chord_event(const struct zmk_position_state_changed *ev) {
    if (ev->position >= ZMK_KEYMAP_LEN) {
        return -EINVAL;
    }
    nocfree_chord_flush_due();
    const struct zmk_behavior_binding *binding =
        zmk_keymap_get_layer_binding_at_idx(0, ev->position);
    bool modifier = binding && binding->behavior_dev &&
        ((!strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(kp))) &&
          binding->param1 >= LCTRL && binding->param1 <= RGUI) ||
         !strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(mo))));
    if (modifier && ev->state) {
        /* A second press at this position must not overtake its queued release. */
        unsigned flush = 0;
        for (unsigned i = 0; i < nocfree_chord_count; i++) {
            unsigned pos = (nocfree_chord_head + i) % ARRAY_SIZE(nocfree_chords);
            if (nocfree_chords[pos].position == ev->position) {
                flush = i + 1;
            }
        }
        while (flush--) {
            nocfree_chord_pop();
        }
        return zmk_keymap_position_state_changed(ev->source, ev->position, true, ev->timestamp);
    }
    /* Pressure degrades the grace window, never discards a press or release. */
    if (nocfree_chord_count == ARRAY_SIZE(nocfree_chords)) {
        nocfree_chord_pop();
    }
    unsigned tail = (nocfree_chord_head + nocfree_chord_count++) % ARRAY_SIZE(nocfree_chords);
    nocfree_chords[tail] = *ev;
    nocfree_chord_deadlines[tail] = k_uptime_get() + 15;
    k_work_reschedule(&nocfree_chord_work,
                      K_TIMEOUT_ABS_MS(nocfree_chord_deadlines[nocfree_chord_head]));
    return 0;
}
