#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inspect built firmware. Skipped unless a build directory is available.

Point NOCFREE_BUILD_DIR at a directory containing `left/` and `right/` build
trees, or accept the default used by scripts/build-local.sh.
"""

from __future__ import annotations

import os
import re
import struct
import unittest
from pathlib import Path

import ansi_spec as spec

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT.parent / "nocfree-and-zmk-build" / "build"
BUILD = Path(os.environ.get("NOCFREE_BUILD_DIR", DEFAULT_BUILD))

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
UF2_BLOCK = 512

CODE_START, CODE_SIZE = spec.PARTITIONS["code_partition"]
CODE_END = CODE_START + CODE_SIZE


def role_dir(role: str) -> Path:
    return BUILD / role / "zephyr"


def available() -> bool:
    return all((role_dir(role) / ".config").is_file() for role in ("left", "right"))


def kconfig(role: str) -> dict[str, str]:
    out = {}
    for line in (role_dir(role) / ".config").read_text().splitlines():
        match = re.match(r"(CONFIG_\w+)=(.*)", line)
        if match:
            out[match.group(1)] = match.group(2)
    return out


def uf2_blocks(path: Path):
    raw = path.read_bytes()
    assert len(raw) % UF2_BLOCK == 0, f"{path} is not a whole number of UF2 blocks"
    for offset in range(0, len(raw), UF2_BLOCK):
        block = raw[offset : offset + UF2_BLOCK]
        start0, start1, flags, address, payload, _index, _total, family = struct.unpack(
            "<IIIIIIII", block[:32]
        )
        (end,) = struct.unpack("<I", block[508:512])
        yield {
            "start0": start0,
            "start1": start1,
            "flags": flags,
            "address": address,
            "payload": payload,
            "family": family,
            "end": end,
        }


@unittest.skipUnless(available(), f"no build output under {BUILD}")
class ArtifactTest(unittest.TestCase):
    ROLES = ("left", "right")

    def test_scanner_is_compiled_in(self):
        for role in self.ROLES:
            config = kconfig(role)
            with self.subTest(role):
                self.assertEqual(config.get("CONFIG_NOCFREE_KSCAN_PCA9555"), "y")
                self.assertEqual(config.get("CONFIG_ZMK_KSCAN"), "y")
                self.assertEqual(config.get("CONFIG_I2C"), "y")

    def test_split_roles_are_correct(self):
        left, right = kconfig("left"), kconfig("right")
        self.assertEqual(left.get("CONFIG_ZMK_SPLIT"), "y")
        self.assertEqual(right.get("CONFIG_ZMK_SPLIT"), "y")
        self.assertEqual(left.get("CONFIG_ZMK_SPLIT_ROLE_CENTRAL"), "y")
        self.assertNotEqual(right.get("CONFIG_ZMK_SPLIT_ROLE_CENTRAL"), "y")

    def test_only_the_central_has_usb_hid(self):
        self.assertEqual(kconfig("left").get("CONFIG_ZMK_USB"), "y")
        self.assertNotEqual(kconfig("right").get("CONFIG_ZMK_USB"), "y")

    def test_both_halves_have_bluetooth_and_cdc_recovery(self):
        for role in self.ROLES:
            config = kconfig(role)
            with self.subTest(role):
                self.assertEqual(config.get("CONFIG_ZMK_BLE"), "y")
                self.assertEqual(config.get("CONFIG_USB_CDC_ACM"), "y")
                self.assertEqual(config.get("CONFIG_NOCFREE_RECOVERY_CDC_1200_TOUCH"), "y")
                self.assertEqual(config.get("CONFIG_RETENTION_BOOT_MODE"), "y")

    def test_resolved_config_keeps_the_conservative_choices(self):
        """The safety-load-bearing symbols, as Kconfig actually resolved them:
        the internal RC 32 kHz source (an absent crystal stops BLE) and the
        line that enumerates the peripheral's USB recovery interface."""
        for role in self.ROLES:
            config = kconfig(role)
            with self.subTest(role):
                self.assertEqual(config.get("CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC"), "y")
                self.assertNotEqual(config.get("CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL"), "y")
        right = kconfig("right")
        self.assertEqual(right.get("CONFIG_USB_DEVICE_STACK"), "y")
        self.assertEqual(right.get("CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT"), "y")

    def test_split_link_resolved_to_the_robust_phy_and_deep_tx_pipeline(self):
        """CONFIG_ZMK_BLE_EXPERIMENTAL_CONN only requests the 1M PHY; whether
        the controller's 2M PHY actually resolved away is decided by Kconfig
        default ordering. Assert the outcome in the compiled config so a ZMK
        bump that re-enables 2M, or a silently ignored buffer setting, fails
        here instead of on the desk."""
        for role in self.ROLES:
            config = kconfig(role)
            with self.subTest(role):
                self.assertEqual(config.get("CONFIG_ZMK_BLE_EXPERIMENTAL_CONN"), "y")
                self.assertNotEqual(config.get("CONFIG_BT_CTLR_PHY_2M"), "y")
                self.assertEqual(config.get("CONFIG_BT_BUF_ACL_TX_COUNT"), "8")
                self.assertEqual(config.get("CONFIG_BT_L2CAP_TX_BUF_COUNT"), "8")
                self.assertEqual(config.get("CONFIG_BT_CONN_TX_MAX"), "8")
        self.assertEqual(
            kconfig("left").get("CONFIG_ZMK_SPLIT_BLE_CENTRAL_POSITION_QUEUE_SIZE"), "16"
        )
        self.assertEqual(
            kconfig("right").get("CONFIG_ZMK_SPLIT_BLE_PERIPHERAL_POSITION_QUEUE_SIZE"), "32"
        )

    def test_excluded_features_are_absent(self):
        for role in self.ROLES:
            config = kconfig(role)
            for symbol in (
                "CONFIG_ZMK_BACKLIGHT",
                "CONFIG_ZMK_RGB_UNDERGLOW",
                "CONFIG_ZMK_BATTERY_REPORTING",
                "CONFIG_ZMK_STUDIO",
            ):
                with self.subTest(f"{role} {symbol}"):
                    self.assertNotEqual(config.get(symbol), "y")

    def test_application_is_linked_into_the_code_partition(self):
        for role in self.ROLES:
            config = kconfig(role)
            with self.subTest(role):
                self.assertEqual(config.get("CONFIG_USE_DT_CODE_PARTITION"), "y")
                self.assertEqual(int(config["CONFIG_FLASH_LOAD_OFFSET"], 0), CODE_START)
                self.assertEqual(int(config["CONFIG_FLASH_LOAD_SIZE"], 0), CODE_SIZE)

    def test_uf2_writes_only_inside_the_code_partition(self):
        for role in self.ROLES:
            uf2 = role_dir(role) / "zmk.uf2"
            with self.subTest(role):
                self.assertTrue(uf2.is_file(), f"missing {uf2}")
                blocks = list(uf2_blocks(uf2))
                self.assertGreater(len(blocks), 0)

                for block in blocks:
                    self.assertEqual(block["start0"], UF2_MAGIC_START0)
                    self.assertEqual(block["start1"], UF2_MAGIC_START1)
                    self.assertEqual(block["end"], UF2_MAGIC_END)
                    self.assertGreaterEqual(block["address"], CODE_START)
                    self.assertLessEqual(block["address"] + block["payload"], CODE_END)

                lowest = min(b["address"] for b in blocks)
                highest = max(b["address"] + b["payload"] for b in blocks)
                self.assertEqual(lowest, CODE_START)
                self.assertLessEqual(highest, CODE_END)

    def test_uf2_never_targets_the_bootloader_or_factory_data(self):
        fs_start, fs_end = spec.FACTORY_FILESYSTEM
        boot_start = spec.PARTITIONS["boot_partition"][0]
        storage_start = spec.PARTITIONS["storage_partition"][0]

        for role in self.ROLES:
            for block in uf2_blocks(role_dir(role) / "zmk.uf2"):
                first, last = block["address"], block["address"] + block["payload"]
                with self.subTest(f"{role} 0x{first:x}"):
                    self.assertLessEqual(last, storage_start)
                    self.assertFalse(fs_start <= first < fs_end)
                    self.assertLess(first, boot_start)

    def compiled_key_inputs(self, role: str) -> list[tuple[str, int]]:
        """The (expander, bit) list as it appears in the compiled devicetree."""
        text = (role_dir(role) / "zephyr.dts").read_text()
        body = re.search(r"key-inputs = (.*?);$", text, re.M)
        assert body, f"no key-inputs in the {role} devicetree"
        return [
            (label, int(bit, 0))
            for label, bit in re.findall(r"<\s*&(\w+)\s+(0x[0-9a-f]+)\s*>", body.group(1))
        ]

    def test_compiled_devicetree_has_the_exact_key_map(self):
        self.assertEqual(self.compiled_key_inputs("left"), spec.LEFT_INPUTS)
        self.assertEqual(self.compiled_key_inputs("right"), spec.RIGHT_INPUTS)

    def test_compiled_devicetree_excludes_unpopulated_bits(self):
        for role, unused in (("left", spec.LEFT_UNUSED), ("right", spec.RIGHT_UNUSED)):
            declared = set(self.compiled_key_inputs(role))
            for label, bits in unused.items():
                for bit in bits:
                    with self.subTest(f"{role} {label} bit {bit}"):
                        self.assertNotIn((label, bit), declared)

    def test_only_the_peripheral_offsets_its_columns(self):
        left = (role_dir("left") / "zephyr.dts").read_text()
        right = (role_dir("right") / "zephyr.dts").read_text()
        offset = re.search(r"col-offset = <\s*(0x[0-9a-f]+|\d+)\s*>", right)
        self.assertIsNotNone(offset)
        self.assertEqual(int(offset.group(1), 0), spec.RIGHT_COL_OFFSET)
        self.assertNotIn("col-offset", left)

    def test_compiled_transform_covers_every_position(self):
        for role in self.ROLES:
            text = (role_dir(role) / "zephyr.dts").read_text()
            body = re.search(r"map = <(.*?)>;", text, re.S)
            with self.subTest(role):
                self.assertIsNotNone(body)
                values = [int(v, 0) for v in re.findall(r"0x[0-9a-f]+|\b\d+\b", body.group(1))]
                self.assertEqual(sorted(values), sorted(spec.TRANSFORM))

    def test_scanner_and_recovery_code_are_linked(self):
        """Configured is not the same as present in the image."""
        for role in self.ROLES:
            mapfile = (role_dir(role) / "zmk.map").read_text(errors="replace")
            for obj in ("kscan_pca9555.c.obj", "cdc_1200_touch.c.obj"):
                with self.subTest(f"{role} {obj}"):
                    self.assertIn(obj, mapfile)

    def test_uf2_targets_the_nrf52833_family(self):
        """A wrong family ID would let the bootloader reject or misplace it."""
        NRF52833_FAMILY = 0x621E937A
        for role in self.ROLES:
            for block in uf2_blocks(role_dir(role) / "zmk.uf2"):
                with self.subTest(role):
                    self.assertTrue(block["flags"] & UF2_FLAG_FAMILY_ID)
                    self.assertEqual(block["family"], NRF52833_FAMILY)
                break

    def test_expanders_are_at_the_published_addresses(self):
        for role in self.ROLES:
            text = (role_dir(role) / "zephyr.dts").read_text()
            found = sorted(int(a, 16) for a in re.findall(r"keys@(\w+) \{", text))
            with self.subTest(role):
                self.assertEqual(found, sorted(spec.EXPANDER_ADDRESSES.values()))

    def test_image_leaves_headroom_in_the_slot(self):
        for role in self.ROLES:
            size = (role_dir(role) / "zmk.bin").stat().st_size
            with self.subTest(role):
                self.assertLess(size, CODE_SIZE)
                print(f"\n  {role}: {size} bytes of {CODE_SIZE} "
                      f"({100 * size / CODE_SIZE:.1f}%)")


if __name__ == "__main__":
    unittest.main()
