/*
 * Copyright (c) 2026 The NocFree ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Conventional 1200-baud touch recovery.
 *
 * Opening the board's USB CDC interface at 1200 baud requests the preserved
 * UF2 bootloader. This is the same trigger the Arduino/Adafruit tooling uses,
 * and unlike the keymap's &bootloader binding it does not depend on the split
 * link: ZMK reset behaviours act on the half whose key triggered them, so a
 * peripheral that cannot connect to its central -- the main recovery scenario
 * -- has no key-driven path to DFU. This interface is what recovers it.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart/cdc_acm.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(nocfree_cdc_1200_touch, CONFIG_ZMK_LOG_LEVEL);

#define RECOVERY_BAUD_RATE 1200U

/* Any enabled CDC ACM instance serves; the boards define exactly one. */
static const struct device *const cdc =
    DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_cdc_acm_uart));
static struct k_work_delayable reboot_work;

static void reboot_to_bootloader(struct k_work *work) {
    ARG_UNUSED(work);

    int err = bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
    if (err) {
        LOG_ERR("Failed to request the bootloader: %d", err);
        return;
    }

    sys_reboot(SYS_REBOOT_WARM);
}

static void dte_rate_changed(const struct device *dev, uint32_t rate) {
    ARG_UNUSED(dev);

    if (rate == RECOVERY_BAUD_RATE) {
        /*
         * Give the host time to close the port cleanly, and allow the request
         * to be withdrawn if it immediately reconfigures to another rate.
         */
        (void)k_work_reschedule(&reboot_work, K_MSEC(100));
    } else {
        (void)k_work_cancel_delayable(&reboot_work);
    }
}

static int cdc_1200_touch_init(void) {
    if (!device_is_ready(cdc)) {
        LOG_ERR("CDC ACM device is not ready; 1200-baud recovery is unavailable");
        return -ENODEV;
    }

    k_work_init_delayable(&reboot_work, reboot_to_bootloader);
    return cdc_acm_dte_rate_callback_set(cdc, dte_rate_changed);
}

SYS_INIT(cdc_1200_touch_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
