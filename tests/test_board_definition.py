#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate the board devicetree, keymap, metadata and configuration."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

import ansi_spec as spec

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/nocfree/nocfree_and"

LEFT_DTS = BOARD / "nocfree_and_left_nrf52833_zmk.dts"
RIGHT_DTS = BOARD / "nocfree_and_right_nrf52833_zmk.dts"
SHARED_DTSI = BOARD / "nocfree_and.dtsi"
KEYMAP = BOARD / "nocfree_and.keymap"


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def read(path: Path) -> str:
    return strip_comments(path.read_text())


def key_inputs(path: Path) -> list[tuple[str, int]]:
    body = re.search(r"key-inputs\s*=\s*(.*?);", read(path), re.S)
    assert body, f"no key-inputs in {path.name}"
    return [(label, int(bit)) for label, bit in re.findall(r"<&(\w+)\s+(\d+)>", body.group(1))]


def property_value(text: str, node: str, prop: str) -> str | None:
    match = re.search(rf"{re.escape(node)}\s*\{{(.*?)\n\}};", text, re.S)
    if not match:
        return None
    found = re.search(rf"\b{re.escape(prop)}\s*=\s*<([^>]*)>", match.group(1))
    return found.group(1).strip() if found else None


class KeyInputTest(unittest.TestCase):
    """The exact expander bit each ANSI position is wired to."""

    def test_left_inputs_are_exact(self):
        self.assertEqual(key_inputs(LEFT_DTS), spec.LEFT_INPUTS)
        self.assertEqual(len(key_inputs(LEFT_DTS)), spec.LEFT_KEYS)

    def test_right_inputs_are_exact(self):
        self.assertEqual(key_inputs(RIGHT_DTS), spec.RIGHT_INPUTS)
        self.assertEqual(len(key_inputs(RIGHT_DTS)), spec.RIGHT_KEYS)

    def test_no_input_is_declared_twice(self):
        for path in (LEFT_DTS, RIGHT_DTS):
            with self.subTest(path.name):
                inputs = key_inputs(path)
                self.assertEqual(len(inputs), len(set(inputs)))

    def test_unpopulated_expander_bits_are_excluded(self):
        for path, unused in ((LEFT_DTS, spec.LEFT_UNUSED), (RIGHT_DTS, spec.RIGHT_UNUSED)):
            declared = set(key_inputs(path))
            for label, bits in unused.items():
                for bit in bits:
                    with self.subTest(f"{path.name} {label} bit {bit}"):
                        self.assertNotIn((label, bit), declared)

    def test_every_declared_bit_is_within_the_part(self):
        for path in (LEFT_DTS, RIGHT_DTS):
            for label, bit in key_inputs(path):
                with self.subTest(f"{path.name} {label} {bit}"):
                    self.assertIn(bit, spec.ALL_BITS)

    # Single-key resolution and the released state are exercised against the
    # real decoding code in tests/test_kscan_scan.c; re-deriving them here from
    # the same list the exactness tests above already pin would prove nothing.


class TransformTest(unittest.TestCase):
    def test_transform_is_the_full_ansi_order(self):
        body = re.search(r"matrix_transform0:.*?map\s*=\s*<(.*?)>;", read(SHARED_DTSI), re.S)
        self.assertIsNotNone(body)
        actual = [int(v) for v in re.findall(r"RC\(0,(\d+)\)", body.group(1))]
        self.assertEqual(actual, spec.TRANSFORM)
        self.assertEqual(sorted(actual), list(range(spec.TOTAL_KEYS)))

    def test_transform_dimensions_match_the_direct_input_model(self):
        text = read(SHARED_DTSI)
        self.assertEqual(property_value(text, "matrix_transform0: keymap_transform_0", "rows"), "1")
        self.assertEqual(
            property_value(text, "matrix_transform0: keymap_transform_0", "columns"),
            str(spec.TOTAL_KEYS),
        )

    def test_only_the_right_half_offsets_its_columns(self):
        self.assertRegex(read(RIGHT_DTS), rf"col-offset\s*=\s*<{spec.RIGHT_COL_OFFSET}>")
        self.assertNotIn("col-offset", read(LEFT_DTS))

    def test_offset_columns_stay_inside_the_transform(self):
        highest = spec.RIGHT_COL_OFFSET + spec.RIGHT_KEYS - 1
        self.assertEqual(highest, spec.TOTAL_KEYS - 1)


class KeymapTest(unittest.TestCase):
    def layers(self) -> dict[str, list[str]]:
        text = read(KEYMAP)
        out = {}
        for name, body in re.findall(r"(\w+_layer)\s*\{(.*?)\};", text, re.S):
            bindings = re.search(r"bindings\s*=\s*<(.*?)>;", body, re.S)
            if bindings:
                out[name] = [b.strip() for b in re.findall(r"&([^&<>]+)", bindings.group(1))]
        return out

    def test_every_layer_covers_every_position(self):
        layers = self.layers()
        self.assertIn("default_layer", layers)
        self.assertIn("function_layer", layers)
        for name, bindings in layers.items():
            with self.subTest(name):
                self.assertEqual(len(bindings), spec.TOTAL_KEYS)

    def test_default_layer_is_the_expected_ansi_map(self):
        self.assertEqual(self.layers()["default_layer"], spec.DEFAULT_LAYER)

    def test_function_row_matches_the_published_shortcuts(self):
        """Function-row contract from https://www.nocfree.com/pages/nocfree-and-manual."""
        layers = self.layers()
        expected = {
            "kp F1": "kp C_BRI_DN",
            "kp F2": "kp C_BRI_UP",
            "kp F3": "kp C_AC_DESKTOP_SHOW_ALL_WINDOWS",
            "kp F4": "kp C_AC_SEARCH",
            # F5/F6 control the keyboard backlight, which this port cannot safely drive yet.
            "kp F5": "trans",
            "kp F6": "trans",
            "kp F7": "kp C_PREVIOUS",
            "kp F8": "kp C_PLAY_PAUSE",
            "kp F9": "kp C_NEXT",
            "kp F10": "kp C_MUTE",
            "kp F11": "kp C_VOL_DN",
            "kp F12": "kp C_VOL_UP",
        }

        for base_binding, expected_function in expected.items():
            with self.subTest(base_binding):
                position = layers["default_layer"].index(base_binding)
                self.assertEqual(layers["function_layer"][position], expected_function)

    def test_recovery_and_output_bindings_are_reachable(self):
        function = self.layers()["function_layer"]
        for binding in ("bootloader", "sys_reset", "out OUT_USB", "out OUT_BLE", "bt BT_CLR"):
            with self.subTest(binding):
                self.assertIn(binding, function)

    def test_a_function_key_exists_on_both_halves(self):
        """Bluetooth pairing and recovery are only reachable through Fn."""
        default = spec.DEFAULT_LAYER
        left = [default[i] for i, p in enumerate(spec.TRANSFORM) if p < spec.RIGHT_COL_OFFSET]
        right = [default[i] for i, p in enumerate(spec.TRANSFORM) if p >= spec.RIGHT_COL_OFFSET]
        self.assertIn("mo 1", left)
        self.assertIn("mo 1", right)


class FlashLayoutTest(unittest.TestCase):
    """The bootloader, SoftDevice and factory filesystem must stay untouched."""

    def partitions(self) -> dict[str, tuple[int, int, bool]]:
        text = SHARED_DTSI.read_text()
        block = re.search(r"&flash0\s*\{(.*)\n\};", text, re.S)
        assert block
        out = {}
        for label, body in re.findall(r"(\w+):\s*partition@\w+\s*\{(.*?)\}", block.group(1), re.S):
            reg = re.search(r"reg\s*=\s*<\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*>", body)
            assert reg, label
            out[label] = (int(reg.group(1), 16), int(reg.group(2), 16), "read-only" in body)
        return out

    def test_partition_bounds_are_exact(self):
        found = self.partitions()
        for label, (start, size) in spec.PARTITIONS.items():
            with self.subTest(label):
                self.assertIn(label, found)
                self.assertEqual(found[label][0], start)
                self.assertEqual(found[label][1], size)

    def test_softdevice_and_bootloader_are_read_only(self):
        found = self.partitions()
        for label, (_, _, read_only) in found.items():
            with self.subTest(label):
                self.assertEqual(read_only, label in spec.READ_ONLY_PARTITIONS)

    def test_writable_regions_never_reach_the_factory_filesystem(self):
        found = self.partitions()
        fs_start, fs_end = spec.FACTORY_FILESYSTEM
        for label in ("code_partition", "storage_partition"):
            start, size, _ = found[label]
            with self.subTest(label):
                self.assertLessEqual(start + size, fs_start)

        self.assertEqual(found["boot_partition"][0], fs_end)
        self.assertEqual(
            found["boot_partition"][0] + found["boot_partition"][1], spec.FLASH_END
        )

    def test_no_partition_overlaps_another(self):
        ranges = sorted((start, start + size) for start, size, _ in self.partitions().values())
        for (_, end), (next_start, _) in zip(ranges, ranges[1:]):
            self.assertLessEqual(end, next_start)

    def test_the_application_links_into_the_code_partition(self):
        self.assertIn("zephyr,code-partition = &code_partition;", read(SHARED_DTSI))
        for path in (
            BOARD / "nocfree_and_left_nrf52833_zmk_defconfig",
            BOARD / "nocfree_and_right_nrf52833_zmk_defconfig",
        ):
            with self.subTest(path.name):
                self.assertIn("CONFIG_USE_DT_CODE_PARTITION=y", path.read_text())


class BusTest(unittest.TestCase):
    def test_expanders_are_declared_at_the_published_addresses(self):
        pattern = r"(\w+):\s*keys@\w+\s*\{[^}]*?reg\s*=\s*<(0x\w+)>"
        found = {
            label: int(addr, 16)
            for label, addr in re.findall(pattern, read(SHARED_DTSI), re.S)
        }
        self.assertEqual(found, spec.EXPANDER_ADDRESSES)

    def test_bus_uses_the_conservative_standard_mode(self):
        self.assertRegex(read(SHARED_DTSI), r"clock-frequency\s*=\s*<I2C_BITRATE_STANDARD>")

    def test_active_scan_period_exceeds_the_bus_transfer_time(self):
        """A scan that cannot finish inside its own period never idles.

        One two-byte port-pair read is about 48 bit times plus framing. At
        100 kHz that is ~0.5 ms per expander, so three expanders need ~1.5 ms.
        """
        bit_time_us = 1_000_000 / 100_000
        per_expander_us = 48 * bit_time_us
        scan_ms = (per_expander_us * len(spec.EXPANDER_ADDRESSES)) / 1000

        for path in (LEFT_DTS, RIGHT_DTS):
            text = read(path)
            active = int(re.search(r"debounce-scan-period-ms\s*=\s*<(\d+)>", text).group(1))
            idle = int(re.search(r"poll-period-ms\s*=\s*<(\d+)>", text).group(1))
            with self.subTest(path.name):
                self.assertGreater(active, scan_ms)
                self.assertGreaterEqual(idle, active)

    def test_scanner_owns_every_declared_expander(self):
        for path in (LEFT_DTS, RIGHT_DTS):
            text = read(path)
            expanders = re.search(r"expanders\s*=\s*(.*?);", text, re.S)
            self.assertIsNotNone(expanders, path.name)
            declared = set(re.findall(r"&(\w+)", expanders.group(1)))
            used = {label for label, _ in key_inputs(path)}
            with self.subTest(path.name):
                self.assertEqual(declared, set(spec.EXPANDER_ADDRESSES))
                self.assertTrue(used <= declared)


class RoleTest(unittest.TestCase):
    def test_left_is_central_and_right_is_not(self):
        text = (BOARD / "Kconfig.defconfig").read_text()
        left = re.search(r"if BOARD_NOCFREE_AND_LEFT\n(.*?)\nendif", text, re.S).group(1)
        right = re.search(r"if BOARD_NOCFREE_AND_RIGHT\n(.*?)\nendif", text, re.S).group(1)
        self.assertIn("ZMK_SPLIT_ROLE_CENTRAL", left)
        self.assertNotIn("ZMK_SPLIT_ROLE_CENTRAL", right)
        self.assertIn("config ZMK_SPLIT\n    default y", text)

    def test_the_defconfigs_never_set_a_split_role(self):
        """Roles live in Kconfig.defconfig only. A defconfig override here
        would silently make both halves centrals and kill the right half."""
        for name in (
            "nocfree_and_left_nrf52833_zmk_defconfig",
            "nocfree_and_right_nrf52833_zmk_defconfig",
        ):
            with self.subTest(name):
                self.assertNotIn("ZMK_SPLIT_ROLE_CENTRAL", (BOARD / name).read_text())

    def test_only_the_central_presents_usb_hid(self):
        left = (BOARD / "nocfree_and_left_nrf52833_zmk_defconfig").read_text()
        right = (BOARD / "nocfree_and_right_nrf52833_zmk_defconfig").read_text()
        self.assertIn("CONFIG_ZMK_USB=y", left)
        self.assertNotIn("CONFIG_ZMK_USB=y", right)

    def test_both_halves_advertise_bluetooth(self):
        for name in (
            "nocfree_and_left_nrf52833_zmk_defconfig",
            "nocfree_and_right_nrf52833_zmk_defconfig",
        ):
            with self.subTest(name):
                self.assertIn("CONFIG_ZMK_BLE=y", (BOARD / name).read_text())

    def test_both_halves_keep_a_usb_recovery_interface(self):
        for name in (
            "nocfree_and_left_nrf52833_zmk_defconfig",
            "nocfree_and_right_nrf52833_zmk_defconfig",
        ):
            text = (BOARD / name).read_text()
            with self.subTest(name):
                self.assertIn("CONFIG_USB_CDC_ACM=y", text)
                self.assertIn("CONFIG_CDC_ACM_DTE_RATE_CALLBACK_SUPPORT=y", text)
                self.assertIn("CONFIG_RETENTION_BOOT_MODE=y", text)

    def test_the_peripheral_enumerates_usb_without_zmk_usb(self):
        """With CONFIG_ZMK_USB unset, ZMK never calls usb_enable(); these two
        lines are the only thing that brings up the right half's recovery
        interface. Losing either strands the peripheral without a
        connection-independent DFU path."""
        right = (BOARD / "nocfree_and_right_nrf52833_zmk_defconfig").read_text()
        self.assertIn("CONFIG_USB_DEVICE_STACK=y", right)
        self.assertIn("CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=y", right)

    def test_low_frequency_clock_stays_on_the_internal_rc(self):
        """No 32.768 kHz crystal is confirmed fitted. Selecting an absent
        crystal silently stops the BLE controller on both halves."""
        for name in (
            "nocfree_and_left_nrf52833_zmk_defconfig",
            "nocfree_and_right_nrf52833_zmk_defconfig",
        ):
            text = (BOARD / name).read_text()
            with self.subTest(name):
                self.assertIn("CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y", text)
                self.assertNotIn("K32SRC_XTAL", text)

    def test_unverified_hardware_stays_disabled(self):
        """No output pin, regulator mode or radio power is asserted anywhere."""
        text = " ".join(read(p) for p in BOARD.glob("*.dts*"))
        for forbidden in (
            "zmk,backlight",
            "zmk,underglow",
            "zmk,battery",
            "pwm-leds",
            "gpio-leds",
            "regulator-initial-mode",
            # No devicetree mechanism may drive a pin either.
            "gpio-hog",
            "output-high",
            "output-low",
        ):
            with self.subTest(forbidden):
                self.assertNotIn(forbidden, text)

        for name in BOARD.glob("*_defconfig"):
            config = name.read_text()
            for forbidden in (
                "CONFIG_ZMK_BACKLIGHT",
                "CONFIG_ZMK_RGB_UNDERGLOW",
                "CONFIG_BT_CTLR_TX_PWR",
            ):
                with self.subTest(f"{name.name} {forbidden}"):
                    self.assertNotIn(forbidden, config)

            # This one defaults on, so it has to be turned off explicitly.
            with self.subTest(f"{name.name} battery"):
                self.assertIn("CONFIG_ZMK_BATTERY_REPORTING=n", config)


class MetadataTest(unittest.TestCase):
    def test_module_name_follows_the_zmk_convention(self):
        text = (ROOT / "zephyr/module.yml").read_text()
        self.assertIn("name: zmk-keyboard-nocfree-and", text)
        self.assertIn("board_root: .", text)
        self.assertIn("dts_root: .", text)

    def test_board_yml_declares_both_halves_with_the_zmk_variant(self):
        text = (BOARD / "board.yml").read_text()
        for name in ("nocfree_and_left", "nocfree_and_right"):
            self.assertIn(f"name: {name}", text)
        self.assertEqual(text.count("name: nrf52833"), 2)
        self.assertEqual(text.count("name: zmk"), 2)

    def test_hardware_metadata_has_the_required_fields(self):
        text = (BOARD / "nocfree_and.zmk.yml").read_text()
        for required in ('file_format: "1"', "id: nocfree_and", "type: board", "arch: arm"):
            with self.subTest(required):
                self.assertIn(required, text)
        self.assertIn("nocfree_and_left//zmk", text)
        self.assertIn("nocfree_and_right//zmk", text)
        self.assertIn("- keys", text)

    def test_build_matrix_covers_both_roles(self):
        text = (ROOT / "build.yaml").read_text()
        self.assertIn("nocfree_and_left/nrf52833/zmk", text)
        self.assertIn("nocfree_and_right/nrf52833/zmk", text)

    def test_dependencies_are_public_and_pinned(self):
        text = (ROOT / "config/west.yml").read_text()
        self.assertIn("url-base: https://github.com/zmkfirmware", text)
        revision = re.search(r"revision:\s*([0-9a-f]{40})", text)
        self.assertIsNotNone(revision, "ZMK must be pinned to an exact commit")


if __name__ == "__main__":
    unittest.main()
