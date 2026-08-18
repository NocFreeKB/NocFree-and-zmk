/*
 * Copyright (c) 2026 The NocFree ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT nocfree_kscan_pca9555

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/debounce.h>

#include "kscan_pca9555_scan.h"

LOG_MODULE_REGISTER(nocfree_kscan_pca9555, CONFIG_ZMK_LOG_LEVEL);

/*
 * One scan blocks on I2C for as long as it takes to read every expander --
 * roughly 1.5 ms for three parts at 100 kHz. The system work queue is
 * cooperative and is also where ZMK turns key events into HID reports and split
 * notifications, so scanning there would put every scan's bus transfer directly
 * in front of the events it has just produced. Use a dedicated preemptible
 * queue instead.
 */
K_THREAD_STACK_DEFINE(kscan_pca9555_stack, CONFIG_NOCFREE_KSCAN_PCA9555_STACK_SIZE);
static struct k_work_q kscan_pca9555_work_q;

static int kscan_pca9555_work_q_init(void) {
    static const struct k_work_queue_config queue_config = {.name = "pca9555 kscan"};

    k_work_queue_start(&kscan_pca9555_work_q, kscan_pca9555_stack,
                       K_THREAD_STACK_SIZEOF(kscan_pca9555_stack),
                       CONFIG_NOCFREE_KSCAN_PCA9555_PRIORITY, &queue_config);
    return 0;
}

SYS_INIT(kscan_pca9555_work_q_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

struct kscan_pca9555_key {
    /** I2C address of the owning expander, resolved to an index at init. */
    uint8_t addr;
    /** Expander pin, 0-7 for port 0 and 8-15 for port 1. */
    uint8_t bit;
};

struct kscan_pca9555_config {
    const struct i2c_dt_spec *expanders;
    uint8_t expander_count;
    const struct kscan_pca9555_key *keys;
    uint16_t key_count;
    struct zmk_debounce_config debounce;
    uint16_t debounce_scan_period_ms;
    uint16_t poll_period_ms;
};

struct kscan_pca9555_data {
    const struct device *dev;
    kscan_callback_t callback;
    struct k_work_delayable work;
    /** Debounce state, one entry per declared key. */
    struct zmk_debounce_state *debounce_state;
    /** Expander index for each declared key, resolved once at init. */
    uint8_t *key_expander;
    /** Most recent port-pair word for each expander. */
    uint16_t *port_words;
    /** Absolute uptime of the next scan, in milliseconds. */
    int64_t scan_deadline;
    /** Uptime at the start of the previous scan, for real debounce timing. */
    int64_t last_scan_time;
    /** Consecutive failed scans, for retry backoff and log rate limiting. */
    uint8_t fail_streak;
    /** True between enable_callback and disable_callback. */
    bool enabled;
    /** Set once every expander has been configured and read back correctly. */
    bool ready;
};

/* Write a 16-bit value to a PCA9555 register pair. */
static int kscan_pca9555_write_pair(const struct i2c_dt_spec *i2c, uint8_t reg, uint16_t value) {
    uint8_t buf[1 + NOCFREE_PCA9555_PORT_PAIR_BYTES] = {reg};

    nocfree_pca9555_port_bytes(value, &buf[1]);
    return i2c_write_dt(i2c, buf, sizeof(buf));
}

/* Read a 16-bit value from a PCA9555 register pair. */
static int kscan_pca9555_read_pair(const struct i2c_dt_spec *i2c, uint8_t reg, uint16_t *value) {
    uint8_t buf[NOCFREE_PCA9555_PORT_PAIR_BYTES];
    int err = i2c_write_read_dt(i2c, &reg, sizeof(reg), buf, sizeof(buf));

    if (err) {
        return err;
    }

    *value = nocfree_pca9555_port_word(buf);
    return 0;
}

/*
 * Establish and verify the register state this scanner depends on.
 *
 * Both ports are forced to inputs and hardware polarity inversion is forced
 * off. The polarity registers matter for safety: they survive a warm MCU reset
 * while the expanders stay powered, and firmware that leaves them inverting
 * would make every released key read as pressed here. Writing them is not
 * enough on its own, so the values are read back before the scanner is allowed
 * to report anything.
 */
static int kscan_pca9555_configure_expander(const struct i2c_dt_spec *i2c) {
    uint16_t readback;
    int err;

    err = kscan_pca9555_write_pair(i2c, NOCFREE_PCA9555_REG_POLARITY_INVERSION_PORT0,
                                   NOCFREE_PCA9555_POLARITY_NONE);
    if (err) {
        LOG_ERR("0x%02x: polarity write failed: %d", i2c->addr, err);
        return err;
    }

    err = kscan_pca9555_write_pair(i2c, NOCFREE_PCA9555_REG_CONFIGURATION_PORT0,
                                   NOCFREE_PCA9555_CONFIGURATION_ALL_INPUTS);
    if (err) {
        LOG_ERR("0x%02x: configuration write failed: %d", i2c->addr, err);
        return err;
    }

    err = kscan_pca9555_read_pair(i2c, NOCFREE_PCA9555_REG_POLARITY_INVERSION_PORT0, &readback);
    if (err) {
        LOG_ERR("0x%02x: polarity read back failed: %d", i2c->addr, err);
        return err;
    }
    if (readback != NOCFREE_PCA9555_POLARITY_NONE) {
        LOG_ERR("0x%02x: polarity inversion still 0x%04x", i2c->addr, readback);
        return -EIO;
    }

    err = kscan_pca9555_read_pair(i2c, NOCFREE_PCA9555_REG_CONFIGURATION_PORT0, &readback);
    if (err) {
        LOG_ERR("0x%02x: configuration read back failed: %d", i2c->addr, err);
        return err;
    }
    if (readback != NOCFREE_PCA9555_CONFIGURATION_ALL_INPUTS) {
        LOG_ERR("0x%02x: configuration is 0x%04x, not all inputs", i2c->addr, readback);
        return -EIO;
    }

    return 0;
}

static void kscan_pca9555_reschedule(const struct device *dev, uint16_t delay_ms) {
    struct kscan_pca9555_data *data = dev->data;
    const int64_t now = k_uptime_get();

    data->scan_deadline += delay_ms;

    /*
     * A scan can outrun its own period, because every expander read shares one
     * I2C bus. An absolute deadline left in the past re-arms the work
     * immediately, so the deficit compounds and the scan thread never idles.
     * Resynchronise a whole period ahead rather than accumulating the debt.
     */
    if (data->scan_deadline <= now) {
        data->scan_deadline = now + delay_ms;
    }

    k_work_reschedule_for_queue(&kscan_pca9555_work_q, &data->work,
                                K_TIMEOUT_ABS_MS(data->scan_deadline));
}

static int kscan_pca9555_read(const struct device *dev) {
    const struct kscan_pca9555_config *config = dev->config;
    struct kscan_pca9555_data *data = dev->data;
    bool continue_scan = false;

    if (!data->enabled) {
        return 0;
    }

    for (uint8_t i = 0; i < config->expander_count; i++) {
        int err = kscan_pca9555_read_pair(&config->expanders[i],
                                          NOCFREE_PCA9555_REG_INPUT_PORT0, &data->port_words[i]);

        if (err) {
            /*
             * Never let a failed or partial transfer reach the debouncer. Hold
             * the previous key state and retry, backing off so a dead bus is
             * probed about once a second instead of at full scan rate, and log
             * only the edges of the outage so the log stays usable.
             */
            if (data->fail_streak == 0) {
                LOG_ERR("0x%02x: input read failed: %d; retrying with backoff",
                        config->expanders[i].addr, err);
            }
            if (data->fail_streak < UINT8_MAX) {
                data->fail_streak++;
            }

            const uint32_t backoff = MIN(
                (uint32_t)config->poll_period_ms << MIN(data->fail_streak, 7), 1000U);

            kscan_pca9555_reschedule(dev, (uint16_t)backoff);
            return err;
        }
    }

    if (data->fail_streak != 0) {
        LOG_WRN("input reads recovered after %u failed scans", data->fail_streak);
        data->fail_streak = 0;
    }

    const int64_t scan_start = k_uptime_get();
    const int elapsed_ms =
        (int)CLAMP(scan_start - data->last_scan_time, 1, DEBOUNCE_COUNTER_MAX);

    data->last_scan_time = scan_start;

    for (uint16_t column = 0; column < config->key_count; column++) {
        const struct kscan_pca9555_key *key = &config->keys[column];
        struct zmk_debounce_state *state = &data->debounce_state[column];
        const bool active =
            nocfree_pca9555_key_active(data->port_words[data->key_expander[column]], key->bit);
        const int credit_ms = nocfree_pca9555_debounce_credit_ms(
            active, state->pressed, state->counter, elapsed_ms, config->debounce_scan_period_ms);

        zmk_debounce_update(state, active, credit_ms, &config->debounce);
    }

    for (uint16_t column = 0; column < config->key_count; column++) {
        struct zmk_debounce_state *state = &data->debounce_state[column];

        if (zmk_debounce_get_changed(state)) {
            const bool pressed = zmk_debounce_is_pressed(state);

            LOG_DBG("Sending event at 0,%u state %s", column, pressed ? "on" : "off");
            data->callback(dev, 0, column, pressed);
        }

        continue_scan = continue_scan || zmk_debounce_is_active(state);
    }

    kscan_pca9555_reschedule(dev, continue_scan ? config->debounce_scan_period_ms
                                                : config->poll_period_ms);
    return 0;
}

static void kscan_pca9555_work_handler(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct kscan_pca9555_data *data =
        CONTAINER_OF(delayable, struct kscan_pca9555_data, work);

    (void)kscan_pca9555_read(data->dev);
}

static int kscan_pca9555_configure(const struct device *dev, kscan_callback_t callback) {
    struct kscan_pca9555_data *data = dev->data;

    if (!callback) {
        return -EINVAL;
    }

    data->callback = callback;
    return 0;
}

static int kscan_pca9555_enable_callback(const struct device *dev) {
    struct kscan_pca9555_data *data = dev->data;

    /*
     * Fail closed. If any expander could not be configured and verified, the
     * scanner stays silent rather than reporting input it cannot interpret.
     */
    if (!data->ready) {
        LOG_ERR("Refusing to scan: expander configuration was not verified");
        return -EIO;
    }

    if (!data->callback) {
        LOG_ERR("Enabled before a callback was configured");
        return -EINVAL;
    }

    data->enabled = true;
    data->fail_streak = 0;
    data->scan_deadline = k_uptime_get();
    data->last_scan_time = data->scan_deadline;

    /*
     * The first scan runs on the dedicated queue like every other scan, so
     * enabling never blocks the caller -- typically the cooperative system
     * work queue -- on the I2C bus, and every scan runs on one thread.
     */
    k_work_reschedule_for_queue(&kscan_pca9555_work_q, &data->work, K_NO_WAIT);
    return 0;
}

static int kscan_pca9555_disable_callback(const struct device *dev) {
    struct kscan_pca9555_data *data = dev->data;

    /*
     * Clear the flag before cancelling: cancel does not wait for a running
     * handler, and a scan already past its enabled check re-arms itself once.
     * That successor observes the cleared flag and stops without rescheduling.
     */
    data->enabled = false;
    k_work_cancel_delayable(&data->work);
    return 0;
}

static int kscan_pca9555_init(const struct device *dev) {
    const struct kscan_pca9555_config *config = dev->config;
    struct kscan_pca9555_data *data = dev->data;

    data->dev = dev;
    k_work_init_delayable(&data->work, kscan_pca9555_work_handler);

    for (uint8_t i = 0; i < config->expander_count; i++) {
        if (!i2c_is_ready_dt(&config->expanders[i])) {
            LOG_ERR("I2C bus for expander 0x%02x is not ready", config->expanders[i].addr);
            return -ENODEV;
        }

        int err = kscan_pca9555_configure_expander(&config->expanders[i]);
        if (err) {
            return err;
        }
    }

    /* Resolve each declared key to the expander that owns it. */
    for (uint16_t column = 0; column < config->key_count; column++) {
        const uint8_t addr = config->keys[column].addr;
        uint8_t index;

        for (index = 0; index < config->expander_count; index++) {
            if (config->expanders[index].addr == addr) {
                break;
            }
        }

        if (index == config->expander_count) {
            LOG_ERR("Key %u names expander 0x%02x, which is not in expanders", column, addr);
            return -EINVAL;
        }

        data->key_expander[column] = index;
    }

    data->ready = true;
    return 0;
}

static const struct kscan_driver_api kscan_pca9555_api = {
    .config = kscan_pca9555_configure,
    .enable_callback = kscan_pca9555_enable_callback,
    .disable_callback = kscan_pca9555_disable_callback,
};

#define EXPANDER_SPEC(node_id, prop, idx) I2C_DT_SPEC_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx))

#define KEY_ENTRY(node_id, prop, idx)                                                              \
    {                                                                                              \
        .addr = DT_REG_ADDR(DT_PHANDLE_BY_IDX(node_id, prop, idx)),                                \
        .bit = DT_PHA_BY_IDX(node_id, prop, idx, bit),                                             \
    }

/* Every expander must sit on the controller the scanner reads in one pass. */
#define EXPANDER_BUS_ASSERT(node_id, prop, idx)                                                    \
    BUILD_ASSERT(DT_SAME_NODE(DT_BUS(DT_PHANDLE_BY_IDX(node_id, prop, idx)),                       \
                              DT_BUS(DT_PHANDLE_BY_IDX(node_id, prop, 0))),                        \
                 "every expander must be on the same I2C controller");

#define KEY_BIT_ASSERT(node_id, prop, idx)                                                         \
    BUILD_ASSERT(DT_PHA_BY_IDX(node_id, prop, idx, bit) < NOCFREE_PCA9555_PINS,                    \
                 "key-inputs bit must be 0-15");

#define KSCAN_PCA9555_INIT(n)                                                                      \
    BUILD_ASSERT(DT_INST_PROP(n, debounce_press_ms) <= DEBOUNCE_COUNTER_MAX,                       \
                 "debounce-press-ms is too large");                                                \
    BUILD_ASSERT(DT_INST_PROP(n, debounce_release_ms) <= DEBOUNCE_COUNTER_MAX,                     \
                 "debounce-release-ms is too large");                                              \
    BUILD_ASSERT(DT_INST_PROP(n, debounce_scan_period_ms) > 0,                                     \
                 "debounce-scan-period-ms must be greater than zero");                             \
    BUILD_ASSERT(DT_INST_PROP(n, poll_period_ms) > 0,                                              \
                 "poll-period-ms must be greater than zero");                                      \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, expanders) > 0, "at least one expander is required");         \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, key_inputs) > 0, "at least one key input is required");       \
    DT_INST_FOREACH_PROP_ELEM(n, expanders, EXPANDER_BUS_ASSERT)                                   \
    DT_INST_FOREACH_PROP_ELEM(n, key_inputs, KEY_BIT_ASSERT)                                       \
                                                                                                   \
    static const struct i2c_dt_spec kscan_pca9555_expanders_##n[] = {                              \
        DT_INST_FOREACH_PROP_ELEM_SEP(n, expanders, EXPANDER_SPEC, (, ))};                         \
    static const struct kscan_pca9555_key kscan_pca9555_keys_##n[] = {                             \
        DT_INST_FOREACH_PROP_ELEM_SEP(n, key_inputs, KEY_ENTRY, (, ))};                            \
    static struct zmk_debounce_state                                                               \
        kscan_pca9555_debounce_##n[ARRAY_SIZE(kscan_pca9555_keys_##n)];                            \
    static uint8_t kscan_pca9555_key_expander_##n[ARRAY_SIZE(kscan_pca9555_keys_##n)];             \
    static uint16_t kscan_pca9555_port_words_##n[ARRAY_SIZE(kscan_pca9555_expanders_##n)];         \
                                                                                                   \
    static struct kscan_pca9555_data kscan_pca9555_data_##n = {                                    \
        .debounce_state = kscan_pca9555_debounce_##n,                                              \
        .key_expander = kscan_pca9555_key_expander_##n,                                            \
        .port_words = kscan_pca9555_port_words_##n,                                                \
    };                                                                                             \
                                                                                                   \
    static const struct kscan_pca9555_config kscan_pca9555_config_##n = {                          \
        .expanders = kscan_pca9555_expanders_##n,                                                  \
        .expander_count = ARRAY_SIZE(kscan_pca9555_expanders_##n),                                 \
        .keys = kscan_pca9555_keys_##n,                                                            \
        .key_count = ARRAY_SIZE(kscan_pca9555_keys_##n),                                           \
        .debounce =                                                                                \
            {                                                                                      \
                .debounce_press_ms = DT_INST_PROP(n, debounce_press_ms),                           \
                .debounce_release_ms = DT_INST_PROP(n, debounce_release_ms),                       \
            },                                                                                     \
        .debounce_scan_period_ms = DT_INST_PROP(n, debounce_scan_period_ms),                       \
        .poll_period_ms = DT_INST_PROP(n, poll_period_ms),                                         \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, &kscan_pca9555_init, NULL, &kscan_pca9555_data_##n,                   \
                          &kscan_pca9555_config_##n, POST_KERNEL,                                  \
                          CONFIG_NOCFREE_KSCAN_PCA9555_INIT_PRIORITY, &kscan_pca9555_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_PCA9555_INIT)
