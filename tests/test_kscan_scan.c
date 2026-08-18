/*
 * Copyright (c) 2026 The NocFree ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Host test for the scanner's decoding and debounce-credit logic. This
 * compiles the same header the firmware uses, with no Zephyr dependency.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../drivers/kscan/kscan_pca9555_scan.h"

static int failures;
static int checks;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                               \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                 \
    do {                                                                                           \
        checks++;                                                                                  \
        long _a = (long)(actual), _e = (long)(expected);                                           \
        if (_a != _e) {                                                                            \
            failures++;                                                                            \
            printf("  FAIL %s:%d: %s == %ld, expected %ld\n", __FILE__, __LINE__, #actual, _a,     \
                   _e);                                                                            \
        }                                                                                          \
    } while (0)

static void test_register_map(void) {
    printf("register map\n");
    /* PCA9555 command bytes. Port pairs are adjacent and auto-increment. */
    CHECK_EQ(NOCFREE_PCA9555_REG_INPUT_PORT0, 0x00);
    CHECK_EQ(NOCFREE_PCA9555_REG_OUTPUT_PORT0, 0x02);
    CHECK_EQ(NOCFREE_PCA9555_REG_POLARITY_INVERSION_PORT0, 0x04);
    CHECK_EQ(NOCFREE_PCA9555_REG_CONFIGURATION_PORT0, 0x06);

    /* The values the driver enforces are the part's power-on values. */
    CHECK_EQ(NOCFREE_PCA9555_POLARITY_NONE, 0x0000);
    CHECK_EQ(NOCFREE_PCA9555_CONFIGURATION_ALL_INPUTS, 0xffff);
}

static void test_port_word_layout(void) {
    printf("port word layout\n");
    /* Port 0 arrives first and occupies the low byte. */
    const uint8_t bytes[2] = {0x3c, 0xa5};
    CHECK_EQ(nocfree_pca9555_port_word(bytes), 0xa53c);

    uint8_t out[2];
    nocfree_pca9555_port_bytes(0xa53c, out);
    CHECK_EQ(out[0], 0x3c);
    CHECK_EQ(out[1], 0xa5);

    for (uint32_t value = 0; value <= 0xffff; value += 7) {
        uint8_t round[2];
        nocfree_pca9555_port_bytes((uint16_t)value, round);
        CHECK_EQ(nocfree_pca9555_port_word(round), value);
    }
}

static void test_keys_are_active_low(void) {
    printf("keys are active low\n");

    /* Nothing pressed: every input is held high by the internal pull-up. */
    for (uint8_t bit = 0; bit < NOCFREE_PCA9555_PINS; bit++) {
        CHECK(!nocfree_pca9555_key_active(0xffff, bit));
    }

    /* One key down pulls exactly one input low. */
    for (uint8_t bit = 0; bit < NOCFREE_PCA9555_PINS; bit++) {
        const uint16_t word = (uint16_t)(0xffff & ~(1u << bit));

        for (uint8_t other = 0; other < NOCFREE_PCA9555_PINS; other++) {
            CHECK_EQ(nocfree_pca9555_key_active(word, other), other == bit);
        }
    }

    /* Every key down reads low on every bit. */
    for (uint8_t bit = 0; bit < NOCFREE_PCA9555_PINS; bit++) {
        CHECK(nocfree_pca9555_key_active(0x0000, bit));
    }

    /*
     * Demonstrate why decoding alone cannot guard against stale hardware
     * polarity inversion: a fully released board (raw 0xffff) reads back
     * through inverting expanders as exactly the all-pressed word above, so
     * the two states are indistinguishable at this layer. The only defence is
     * the driver's write-and-read-back of the polarity registers in
     * kscan_pca9555_configure_expander(), which refuses to scan at all if the
     * registers cannot be verified.
     */
    const uint16_t stale_inverted_release = (uint16_t)~0xffff;
    CHECK_EQ(stale_inverted_release, 0x0000);
    for (uint8_t bit = 0; bit < NOCFREE_PCA9555_PINS; bit++) {
        CHECK(nocfree_pca9555_key_active(stale_inverted_release, bit));
    }
}

static void test_debounce_credit(void) {
    printf("debounce credit\n");
    const int scan_period = 3;
    const int idle_period = 10;

    /*
     * A press first seen during an idle poll must not be credited the whole
     * idle period. With a 5 ms threshold that would latch immediately.
     */
    CHECK_EQ(nocfree_pca9555_debounce_credit_ms(true, false, 0, idle_period, scan_period),
             scan_period);

    /* A release first seen during an idle poll is clamped the same way. */
    CHECK_EQ(nocfree_pca9555_debounce_credit_ms(false, true, 0, idle_period, scan_period),
             scan_period);

    /* Once the transition has survived a sample, real elapsed time is used. */
    CHECK_EQ(nocfree_pca9555_debounce_credit_ms(true, false, 3, idle_period, scan_period),
             idle_period);

    /* A stable key always integrates real elapsed time. */
    CHECK_EQ(nocfree_pca9555_debounce_credit_ms(true, true, 0, idle_period, scan_period),
             idle_period);
    CHECK_EQ(nocfree_pca9555_debounce_credit_ms(false, false, 0, idle_period, scan_period),
             idle_period);

    /* A scan that arrives early is never credited more than it waited. */
    CHECK_EQ(nocfree_pca9555_debounce_credit_ms(true, false, 0, 1, scan_period), 1);

    /*
     * Full sequence: a press seen at an idle poll takes two active scans to
     * latch at a 5 ms threshold, not one.
     */
    int accumulated = nocfree_pca9555_debounce_credit_ms(true, false, 0, idle_period, scan_period);
    CHECK(accumulated < 5);
    accumulated +=
        nocfree_pca9555_debounce_credit_ms(true, false, (uint16_t)accumulated, scan_period,
                                           scan_period);
    CHECK(accumulated >= 5);
}

int main(void) {
    test_register_map();
    test_port_word_layout();
    test_keys_are_active_low();
    test_debounce_credit();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
