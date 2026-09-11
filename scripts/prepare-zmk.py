#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply the reviewed patch to verified copies; never modify the west checkout."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def module_inputs(root: Path):
    paths = [root / "CMakeLists.txt", root / "Kconfig"]
    for directory in ("boards", "config", "drivers", "dts", "src", "cmake", "patches", "zephyr"):
        paths.extend(p for p in (root / directory).rglob("*") if p.is_file())
    paths.append(root / "scripts/prepare-zmk.py")
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths)}


def prepare(source: Path, output: Path):
    root = Path(__file__).resolve().parents[1]
    hashes = json.loads((root / "patches/upstream-sha256.json").read_text())
    for name, expected in hashes.items():
        raw = (source / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise SystemExit(f"Unreviewed ZMK source: {name}; update and test the patch first")
    output.mkdir(parents=True, exist_ok=True)
    for name in hashes:
        dest = output / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, dest)
    subprocess.run(
        ["git", "apply", str(root / "patches/zmk-reliability.patch")], check=True,
        cwd=output, env=dict(os.environ, GIT_CEILING_DIRECTORIES=str(output.resolve().parent)),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    for name, original in hashes.items():
        if hashlib.sha256((output / name).read_bytes()).hexdigest() == original:
            raise SystemExit(f"Reliability patch was not applied to {name}")
    evidence = {
        "module_inputs": module_inputs(root),
        "patched_sources": {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
                            for name in hashes},
    }
    (output / "inputs.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    prepare(Path(sys.argv[1]), Path(sys.argv[2]))
