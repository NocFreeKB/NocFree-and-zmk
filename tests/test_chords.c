/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
/* Included after the production chord implementation by test_runtime.py. */
static void reset_chords(void) {
    nocfree_chord_count = nocfree_chord_head = 0;
    output_count = 0;
    clock_ms = 0;
}
static void input(unsigned position, bool state, int time) {
    clock_ms = time;
    struct zmk_position_state_changed ev = {
        .position = position, .state = state, .timestamp = time,
        .source = position == 0 ? 1 : 0,
    };
    assert(nocfree_chord_event(&ev) == 0);
}
int main(void) {
    reset_chords();
    input(1, true, 0);  /* Local letter arrives before remote Shift. */
    input(0, true, 10);
    assert(output_count == 1 && output[0].position == 0 && output[0].state);
    input(0, false, 11);
    input(1, false, 12);
    clock_ms = 30;
    nocfree_chord_work_cb(NULL);
    assert(output_count == 4);
    assert(output[1].position == 1 && output[1].state);
    assert(output[2].position == 0 && !output[2].state);
    assert(output[3].position == 1 && !output[3].state);
    reset_chords();
    input(1, true, 0);
    input(2, true, 14); /* Fn gets the same grace as Shift. */
    clock_ms = 15;
    nocfree_chord_work_cb(NULL);
    assert(output_count == 2 && output[0].position == 2 && output[1].position == 1);
    reset_chords();
    input(1, true, 0);
    input(0, true, 16); /* Outside grace: preserve ordinary letter semantics. */
    assert(output_count == 2 && output[0].position == 1 && output[1].position == 0);
    reset_chords();
    input(0, true, 0);
    input(0, false, 1);
    input(0, true, 2); /* Re-press must not overtake its release. */
    assert(output_count == 3 && !output[1].state && output[2].state);
    reset_chords();
    for (int i = 0; i < 300; i++) {
        input(1, (i % 2) == 0, 0);
    }
    clock_ms = 16;
    nocfree_chord_work_cb(NULL);
    assert(output_count == 300);
    for (int i = 0; i < 300; i++) {
        assert(output[i].state == ((i % 2) == 0));
    }
    puts("chords: modifier/Fn skew, grace boundary, re-press and overflow passed");
}
