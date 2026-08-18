#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Repository hygiene: licensing, formatting, and no private or generated data."""

from __future__ import annotations

import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Files that carry an upstream licence statement of their own, or that have no
# comment syntax to put an SPDX tag in.
SPDX_EXEMPT = {"LICENSE", "README.md", ".gitignore"}
SPDX_EXEMPT_SUFFIXES = {".md", ".json"}

BINARY_SUFFIXES = {
    ".uf2", ".bin", ".hex", ".elf", ".o", ".a", ".so", ".dylib", ".zip", ".gz",
    ".png", ".jpg", ".jpeg", ".pdf", ".exe", ".dll", ".pyc",
}


def tracked_files() -> list[Path]:
    out = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files"], capture_output=True, text=True, check=True
    ).stdout.split()
    return [ROOT / name for name in out]


def candidate_files() -> list[Path]:
    """Tracked files plus anything new in the working tree, minus ignored paths."""
    tracked = set(tracked_files())
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if rel.parts[0] in {".git", ".jig", "__pycache__"}:
            continue
        ignored = subprocess.run(
            ["git", "-C", str(ROOT), "check-ignore", "-q", str(path)]
        ).returncode == 0
        if not ignored:
            tracked.add(path)
    return sorted(tracked)


class LicensingTest(unittest.TestCase):
    def test_every_source_file_declares_its_licence(self):
        for path in candidate_files():
            rel = path.relative_to(ROOT)
            if str(rel) in SPDX_EXEMPT or path.suffix in SPDX_EXEMPT_SUFFIXES:
                continue
            with self.subTest(str(rel)):
                head = path.read_text(errors="replace")[:600]
                self.assertIn("SPDX-License-Identifier: MIT", head)

    def test_licence_is_unmodified_mit(self):
        text = (ROOT / "LICENSE").read_text()
        self.assertIn("MIT License", text)
        self.assertIn("Copyright (c) 2026 NocFreeKB", text)
        self.assertIn('THE SOFTWARE IS PROVIDED "AS IS"', text)

    def test_no_copyright_claims_conflict_with_the_repository_licence(self):
        for path in candidate_files():
            if path.suffix not in {".c", ".h"}:
                continue
            text = path.read_text()
            with self.subTest(str(path.relative_to(ROOT))):
                self.assertIn("The NocFree ZMK Contributors", text)


class NoPrivateDataTest(unittest.TestCase):
    """Nothing personal, local, vendor-derived or generated may ship."""

    FORBIDDEN = [
        (r"/Users/[A-Za-z0-9._-]+", "a local filesystem path"),
        (r"/home/[A-Za-z0-9._-]+", "a local filesystem path"),
        (r"C:\\\\Users\\\\", "a local filesystem path"),
        (r"\b(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b", "a MAC or Bluetooth address"),
        (r"\bnocfree-zmk\b", "the private reference repository"),
        (r"\bNF-0\d\d\b", "a private work item"),
        (r"\bdiagnostics/\b", "a private evidence directory"),
        (r"\bartifacts/\b", "a generated artifact directory"),
        (r"\.work/", "a private workspace directory"),
        (r"\b239[aA]:00\d\d\b", "an observed USB device identifier"),
        (r"(?i:jcugley|jarrod)", "a personal identifier"),
    ]

    def test_no_forbidden_content(self):
        for path in candidate_files():
            rel = str(path.relative_to(ROOT))
            if rel.startswith("tests/test_repository_hygiene.py"):
                continue  # this file necessarily contains the patterns
            text = path.read_text(errors="replace")
            for pattern, description in self.FORBIDDEN:
                with self.subTest(f"{rel}: {description}"):
                    self.assertIsNone(
                        re.search(pattern, text), f"{rel} contains {description}"
                    )

    def test_no_binaries_or_build_output_are_tracked(self):
        for path in tracked_files():
            with self.subTest(str(path.relative_to(ROOT))):
                self.assertNotIn(path.suffix, BINARY_SUFFIXES)

    def test_every_tracked_file_is_text(self):
        for path in tracked_files():
            with self.subTest(str(path.relative_to(ROOT))):
                self.assertNotIn(b"\x00", path.read_bytes())

    def test_no_vendor_firmware_or_capture_is_present(self):
        for path in candidate_files():
            name = path.name.lower()
            with self.subTest(name):
                self.assertFalse(name.endswith((".uf2", ".pcap", ".pcapng", ".dump")))


class FormattingTest(unittest.TestCase):
    TEXT_SUFFIXES = {".c", ".h", ".dts", ".dtsi", ".keymap", ".yaml", ".yml", ".py",
                     ".sh", ".md", ".cmake", ".txt", ".conf"}

    # README.md is NocFree's published porting guide. This contribution does not
    # reformat it, so it is exempt from the style rules below.
    NOT_OURS = {"README.md"}

    def files(self) -> list[Path]:
        return [
            p for p in candidate_files()
            if str(p.relative_to(ROOT)) not in self.NOT_OURS
            and (p.suffix in self.TEXT_SUFFIXES or p.name in {"Kconfig", "CMakeLists.txt"}
                 or p.name.endswith("_defconfig") or p.name.startswith("Kconfig"))
        ]

    def test_no_trailing_whitespace(self):
        for path in self.files():
            for number, line in enumerate(path.read_text().splitlines(), 1):
                with self.subTest(f"{path.relative_to(ROOT)}:{number}"):
                    self.assertEqual(line, line.rstrip())

    def test_files_end_with_exactly_one_newline(self):
        for path in self.files():
            raw = path.read_bytes()
            with self.subTest(str(path.relative_to(ROOT))):
                self.assertTrue(raw.endswith(b"\n"))
                self.assertFalse(raw.endswith(b"\n\n"))

    def test_no_carriage_returns(self):
        for path in self.files():
            with self.subTest(str(path.relative_to(ROOT))):
                self.assertNotIn(b"\r", path.read_bytes())

    def test_no_tabs_in_devicetree_or_c_sources(self):
        for path in self.files():
            if path.suffix not in {".c", ".h", ".dts", ".dtsi", ".keymap"}:
                continue
            with self.subTest(str(path.relative_to(ROOT))):
                self.assertNotIn("\t", path.read_text())

    def test_lines_are_not_excessively_long(self):
        allowance = {".dts", ".dtsi", ".keymap"}
        for path in self.files():
            limit = 130 if path.suffix in allowance else 100
            for number, line in enumerate(path.read_text().splitlines(), 1):
                # A Markdown table row cannot be wrapped without breaking it,
                # and a workflow `uses:` pinned to a full commit SHA cannot be
                # shortened without losing the pin.
                if path.suffix == ".md" and line.lstrip().startswith("|"):
                    limit = max(limit, 120)
                elif path.suffix == ".yml" and line.lstrip().startswith("uses:"):
                    limit = max(limit, 130)
                else:
                    limit = 130 if path.suffix in allowance else 100
                with self.subTest(f"{path.relative_to(ROOT)}:{number}"):
                    self.assertLessEqual(len(line), limit)


class UpstreamReadmeTest(unittest.TestCase):
    """The published porting guide must survive this contribution intact."""

    def test_original_sections_are_unchanged(self):
        text = (ROOT / "README.md").read_text()
        for heading in (
            "## 1. Disclaimer and No-Warranty Notice",
            "## 3. Hardware Architecture",
            "## 4. Pins Required for ZMK Porting",
            "## 7. Pre-Flash Checklist",
            "## 8. License and Scope",
        ):
            with self.subTest(heading):
                self.assertIn(heading, text)

    def test_the_guides_own_facts_are_not_contradicted(self):
        text = (ROOT / "README.md").read_text()
        self.assertIn("`0x20`, `0x22`, `0x24`", text)
        self.assertIn("P0.11", text)
        self.assertIn("P1.09", text)


if __name__ == "__main__":
    unittest.main()
