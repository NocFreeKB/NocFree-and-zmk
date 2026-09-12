#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Execute production logic with deterministic transport and clock failures."""
import json
import runpy
import shutil
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"


def without_includes(text):
    return re.sub(r"^#include .*\n", "", text, flags=re.M)


def compile_run(text):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "test.c"
        binary = Path(directory) / "test"
        source.write_text(text)
        sanitizers = (["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                      if os.environ.get("NOCFREE_SANITIZE") else [])
        subprocess.run([os.environ.get("CC", "cc"), *sanitizers,
                        "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
                        "-I", str(ROOT / "src/reliability"), "-I", str(TESTS),
                        str(source), "-o", str(binary)], check=True, capture_output=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)


class RuntimeTest(unittest.TestCase):
    def test_state_queue_faults(self):
        compile_run((TESTS / "test_state_queue.c").read_text().replace(
            '../src/reliability/state_queue.h', 'state_queue.h'))

    def test_cross_half_chords(self):
        compile_run('#include "chord_stubs.h"\n' + without_includes(
            (ROOT / "src/reliability/chords.inc").read_text()) +
            (TESTS / "test_chords.c").read_text())

    def test_scanner_faults(self):
        source = (ROOT / "drivers/kscan/kscan_pca9555.c").read_text()
        source = source[:source.index("#define EXPANDER_SPEC")]
        # Device-tree instance generation is exercised by the three firmware builds.
        source = source[:source.index("static const struct kscan_driver_api")]
        compile_run('#include "scanner_stubs.h"\n' + without_includes(source) +
                    (TESTS / "test_scanner_runtime.c").read_text())

    def test_transport_faults(self):
        source = '#include "transport_stubs.h"\n#include "state_queue.h"\n'
        for name in ("split_tx.inc", "hog_tx.inc", "usb_tx.inc"):
            source += without_includes((ROOT / "src/reliability" / name).read_text())
        compile_run(source + (TESTS / "test_transport_runtime.c").read_text())

    @unittest.skipUnless(os.environ.get("NOCFREE_BUILD_DIR"), "requires patched firmware tree")
    def test_central_receive_and_reconnect(self):
        build = Path(os.environ["NOCFREE_BUILD_DIR"])
        central = (build / "left/nocfree-patched/split/bluetooth/central.c").read_text()
        source = '#include "central_stubs.h"\n#include "state_queue.h"\n'
        source += without_includes((ROOT / "src/reliability/central_rx.inc").read_text())
        for name in ("start_scanning", "split_central_notify_func"):
            source += extract_function(central, name)
        source += without_includes((ROOT / "src/reliability/central_rx_work.inc").read_text())
        compile_run(source + (TESTS / "test_central_runtime.c").read_text())

    @unittest.skipUnless(os.environ.get("NOCFREE_BUILD_DIR"), "requires patched firmware tree")
    def test_usb_dma_lifetime_and_backpressure(self):
        build = Path(os.environ["NOCFREE_BUILD_DIR"])
        usb = (build / "left/nocfree-patched/usb_hid.c").read_text()
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#define K_NO_WAIT 0
#define USB_DC_SUSPEND 1
#define USB_DC_ERROR 2
#define USB_DC_RESET 3
#define USB_DC_DISCONNECTED 4
#define USB_DC_UNKNOWN 5
#define __aligned(n) __attribute__((aligned(n)))
static int hid_sem = 1, usb_status, write_error, writes;
static void *hid_dev;
static const uint8_t *dma_pointer;
static int k_sem_take(int *s, int t) { if (!*s) return -EAGAIN; *s = 0; return 0; }
static void k_sem_give(int *s) { *s = 1; }
static int zmk_usb_get_status(void) { return usb_status; }
static int hid_int_ep_write(void *dev, const uint8_t *data, size_t n, void *unused) {
    writes++;
    if (write_error) return write_error;
    dma_pointer = data;
    return 0;
}
'''
        start = usb.index("static uint8_t nocfree_usb_in_flight")
        end = usb.index("static int zmk_usb_hid_send_report", start)
        source += usb[start:end] + extract_function(usb, "zmk_usb_hid_send_report")
        source += r'''
int main(void) {
    uint8_t report[2] = {1, 42};
    assert(zmk_usb_hid_send_report(report, sizeof(report)) == 0);
    assert(dma_pointer != report && dma_pointer[1] == 42);
    report[1] = 99;
    assert(dma_pointer[1] == 42);
    assert(zmk_usb_hid_send_report(report, sizeof(report)) == -EAGAIN && writes == 1);
    k_sem_give(&hid_sem); /* Resume callback restores the semaphore. */
    write_error = -EAGAIN; /* Driver still owns the earlier DMA buffer. */
    assert(zmk_usb_hid_send_report(report, sizeof(report)) == -EAGAIN);
    assert(dma_pointer[1] == 42 && hid_sem == 1);
    write_error = 0;
    assert(zmk_usb_hid_send_report(report, sizeof(report)) == 0);
    assert(dma_pointer[1] == 99);
    usb_status = USB_DC_SUSPEND;
    assert(zmk_usb_hid_send_report(report, sizeof(report)) == -EAGAIN);
    assert(writes == 3);
}
'''
        compile_run(source)

    @unittest.skipUnless(os.environ.get("NOCFREE_BUILD_DIR"), "requires patched firmware tree")
    def test_advertising_retry(self):
        build = Path(os.environ["NOCFREE_BUILD_DIR"])
        peripheral = (build / "right/nocfree-patched/split/bluetooth/peripheral.c").read_text()
        source = r'''#include "transport_stubs.h"
#define LOG_WRN(...)
#define K_SECONDS(t) ((t) * 1000)
static bool enabled, is_connected, low_duty_advertising;
static int advertising_error, advertising_calls;
static struct k_work_delayable advertising_work;
static int start_advertising(bool low) { advertising_calls++; return advertising_error; }
'''
        source += extract_function(peripheral, "advertising_cb")
        source += r'''
int main(void) {
    enabled = true;
    advertising_error = -ENOMEM;
    advertising_cb(NULL);
    assert(advertising_calls == 1 && advertising_work.delay == 1000);
    advertising_error = 0;
    advertising_cb(NULL);
    assert(advertising_calls == 2);
    enabled = false;
    advertising_cb(NULL);
    assert(advertising_calls == 2);
    enabled = true;
    is_connected = true;
    advertising_cb(NULL);
    assert(advertising_calls == 2);
}
'''
        compile_run(source)

    @unittest.skipUnless(os.environ.get("NOCFREE_BUILD_DIR"), "requires patched firmware tree")
    def test_patch_is_repeatable_and_rejects_changed_upstream(self):
        helpers = runpy.run_path(str(ROOT / "scripts/prepare-zmk.py"))
        patched = Path(os.environ["NOCFREE_BUILD_DIR"]) / "left/nocfree-patched"
        names = json.loads((ROOT / "patches/upstream-sha256.json").read_text())
        with tempfile.TemporaryDirectory() as temp:
            original, output = Path(temp) / "original", Path(temp) / "output"
            for name in names:
                dest = original / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(patched / name, dest)
            subprocess.run(["git", "apply", "--reverse",
                            str(ROOT / "patches/zmk-reliability.patch")], check=True,
                           cwd=original, capture_output=True)
            before = {name: (original / name).read_bytes() for name in names}
            for attempt in range(2):
                helpers["prepare"](original, output)
                for name in names:
                    self.assertEqual((output / name).read_bytes(), (patched / name).read_bytes())
                    self.assertEqual((original / name).read_bytes(), before[name])
            (original / "ble.c").write_text("unreviewed change")
            with self.assertRaisesRegex(SystemExit, "Unreviewed ZMK source"):
                helpers["prepare"](original, output)


def extract_function(source, name):
    match = re.search(r"^static [^;{}]*\b" + name + r"\([^;{}]*\)\s*\{", source, re.M)
    if not match:
        raise AssertionError(f"missing production function {name}")
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"
