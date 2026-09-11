/* Copyright (c) 2026 The NocFree ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
static int presses, releases;
static void report(const struct device *dev, uint32_t row, uint32_t col, bool pressed) {
    if (pressed) { presses++; } else { releases++; }
}
int main(void) {
    const struct i2c_dt_spec expanders[] = {{0}, {1}};
    const struct kscan_pca9555_key keys[] = {{0, 0}, {1, 0}};
    struct zmk_debounce_state states[2] = {0};
    uint8_t owners[2];
    uint16_t words[2];
    struct kscan_pca9555_config config = {
        .expanders = expanders, .expander_count = 2, .keys = keys, .key_count = 2,
        .poll_period_ms = 5, .debounce_scan_period_ms = 2,
    };
    struct kscan_pca9555_data data = {
        .debounce_state = states, .key_expander = owners, .port_words = words,
    };
    struct device dev = {&data, &config};
    bus_failed = true;
    assert(kscan_pca9555_init(&dev) == 0);
    assert(kscan_pca9555_configure(&dev, report) == 0);
    assert(kscan_pca9555_enable_callback(&dev) == 0);
    assert(kscan_pca9555_read(&dev) == -EIO);
    assert(!data.ready && presses == 0 && releases == 0);
    bus_failed = false;
    bad_readback = true;
    clock_ms = 20;
    assert(kscan_pca9555_read(&dev) == -EIO);
    assert(!data.ready && presses == 0);
    bad_readback = false;
    clock_ms = 40;
    assert(kscan_pca9555_read(&dev) == 0 && data.ready);
    inputs[0] = inputs[1] = 0xfffe;
    clock_ms = 45;
    assert(kscan_pca9555_read(&dev) == 0 && presses == 2);
    inputs[0] = 0xffff;
    fail_address = 1; /* First expander read succeeds, second fails: no partial events. */
    clock_ms = 50;
    states[0].counter = 3;
    assert(kscan_pca9555_read(&dev) == -EIO && releases == 0);
    assert(states[0].counter == 0);
    clock_ms = 149;
    assert(kscan_pca9555_read(&dev) == -EIO && releases == 0);
    clock_ms = 150;
    assert(kscan_pca9555_read(&dev) == -EIO && releases == 2);
    clock_ms = 250;
    assert(kscan_pca9555_read(&dev) == -EIO && releases == 2);
    fail_address = -1;
    inputs[1] = 0xffff;
    clock_ms = 350;
    assert(kscan_pca9555_read(&dev) == 0 && data.ready && releases == 2);
    inputs[0] = 0xfffe;
    clock_ms = 355;
    assert(kscan_pca9555_read(&dev) == 0 && presses == 3);
    assert(kscan_pca9555_disable_callback(&dev) == 0);
    clock_ms = 400;
    assert(kscan_pca9555_read(&dev) == 0 && presses == 3);
    puts("scanner: boot fault, bad configuration, partial read, release deadline, recovery passed");
}
