/*
 * Copyright (c) 2026 The NocFree ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Pure decoding helpers for the PCA9555 key scanner.
 *
 * This header deliberately depends on nothing but <stdbool.h>/<stdint.h> so the
 * same code that runs on the keyboard is exercised directly by the host test in
 * tests/test_kscan_scan.c.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Command bytes, PCA9555 register map. The part auto-increments within a pair. */
#define NOCFREE_PCA9555_REG_INPUT_PORT0 0x00
#define NOCFREE_PCA9555_REG_OUTPUT_PORT0 0x02
#define NOCFREE_PCA9555_REG_POLARITY_INVERSION_PORT0 0x04
#define NOCFREE_PCA9555_REG_CONFIGURATION_PORT0 0x06

#define NOCFREE_PCA9555_PORT_PAIR_BYTES 2
#define NOCFREE_PCA9555_PINS 16

/* Every I/O pin an input. This is also the part's power-on configuration. */
#define NOCFREE_PCA9555_CONFIGURATION_ALL_INPUTS 0xffffU
/* Hardware polarity inversion fully disabled, matching the power-on value. */
#define NOCFREE_PCA9555_POLARITY_NONE 0x0000U

/** Combine a two-byte port-pair read into one word: port 0 low, port 1 high. */
static inline uint16_t nocfree_pca9555_port_word(const uint8_t bytes[2]) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

/** Split a port word back into the two bytes written over I2C. */
static inline void nocfree_pca9555_port_bytes(uint16_t word, uint8_t bytes[2]) {
    bytes[0] = (uint8_t)(word & 0xffU);
    bytes[1] = (uint8_t)(word >> 8);
}

/**
 * Is the key on @p bit pressed?
 *
 * Key inputs are wired to ground against the part's internal pull-up, so a
 * pressed key reads low. Hardware polarity inversion is held disabled by the
 * driver, so this is the raw input level.
 */
static inline bool nocfree_pca9555_key_active(uint16_t port_word, uint8_t bit) {
    return ((port_word >> bit) & 1U) == 0U;
}

/**
 * Debounce time to credit for one observation, in milliseconds.
 *
 * ZMK's debouncer integrates wall-clock time. When a transition is first seen,
 * the switch may have moved at any point since the previous sample, so the full
 * idle poll period must not be credited as stable time: with a 10 ms idle
 * period and a 5 ms threshold that would latch a press on its very first
 * observation and defeat debouncing entirely. Credit at most one active-scan
 * period until the transition has survived a sample, then integrate real time
 * so a late scan does not distort the latch point either.
 */
static inline int nocfree_pca9555_debounce_credit_ms(bool active, bool latched_pressed,
                                                     uint16_t debounce_counter, int elapsed_ms,
                                                     int scan_period_ms) {
    if (active != latched_pressed && debounce_counter == 0) {
        return elapsed_ms < scan_period_ms ? elapsed_ms : scan_period_ms;
    }

    return elapsed_ms;
}
