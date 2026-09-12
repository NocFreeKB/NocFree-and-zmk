# SPDX-License-Identifier: MIT
"""The single source of truth for the NocFree & ANSI + numpad key map.

Everything the tests assert about the devicetree, the keymap and the built
artifacts is derived from this file, so a change to the board has to be made
here as well as in the devicetree before the suite will pass.
"""

from __future__ import annotations

# The six logical scan rows used by the ANSI halves, in the port order NocFree
# publishes:
#   0x20/P0, 0x20/P1, 0x22/P0, 0x22/P1, 0x24/P0, 0x24/P1
# Expander pins 0-7 are port 0 and pins 8-15 are port 1.
ROWS = [
    ("pca20", 0),
    ("pca20", 8),
    ("pca22", 0),
    ("pca22", 8),
    ("pca24", 0),
    ("pca24", 8),
]

EXPANDER_ADDRESSES = {"pca20": 0x20, "pca22": 0x22, "pca24": 0x24}
SCANNER_EXPANDERS = {
    "left": ("pca20", "pca22", "pca24"),
    "right": ("pca20", "pca22", "pca24"),
    "numpad": ("pca20", "pca22"),
}

# Populated inputs per visual row. Left/right counts are a standard ANSI board
# split between T and Y. The numpad's 4/4/4/3/4/2 groups match its physical
# rows; its electrical order is defined separately below.
LEFT_COUNTS = [7, 7, 6, 6, 6, 5]
RIGHT_COUNTS = [8, 8, 8, 8, 8, 7]
NUMPAD_COUNTS = [4, 4, 4, 3, 4, 2]

LEFT_KEYS = sum(LEFT_COUNTS)      # 37
RIGHT_KEYS = sum(RIGHT_COUNTS)    # 47
NUMPAD_KEYS = sum(NUMPAD_COUNTS)  # 21
TOTAL_KEYS = LEFT_KEYS + RIGHT_KEYS + NUMPAD_KEYS  # 105

RIGHT_COL_OFFSET = LEFT_KEYS
NUMPAD_COL_OFFSET = LEFT_KEYS + RIGHT_KEYS

# All sixteen pins of a PCA9555.
ALL_BITS = set(range(16))


def key_inputs(counts: list[int]) -> list[tuple[str, int]]:
    """(expander, bit) for each KSCAN column, in column order."""
    out: list[tuple[str, int]] = []
    for (label, base), count in zip(ROWS, counts):
        out.extend((label, base + i) for i in range(count))
    return out


LEFT_INPUTS = key_inputs(LEFT_COUNTS)
RIGHT_INPUTS = key_inputs(RIGHT_COUNTS)
# Proven numpad order: the first fifteen inputs of pca20, followed by the
# first six inputs of pca22. The physical unit does not respond at pca24, so
# that unused shared address is deliberately not part of the numpad scanner.
NUMPAD_INPUTS = (
    [("pca20", bit) for bit in range(15)]
    + [("pca22", bit) for bit in range(6)]
)


def unused_bits(inputs: list[tuple[str, int]]) -> dict[str, set[int]]:
    """Expander bits that must NOT appear in a half's key-inputs list."""
    used: dict[str, set[int]] = {label: set() for label, _ in ROWS}
    for label, bit in inputs:
        used[label].add(bit)
    return {label: ALL_BITS - bits for label, bits in used.items()}


LEFT_UNUSED = unused_bits(LEFT_INPUTS)
RIGHT_UNUSED = unused_bits(RIGHT_INPUTS)
NUMPAD_UNUSED = unused_bits(NUMPAD_INPUTS)


def transform_positions() -> list[int]:
    """Visual reading order: left row, right row, then numpad row."""
    out: list[int] = []
    left, right, pad = 0, RIGHT_COL_OFFSET, NUMPAD_COL_OFFSET
    for lc, rc, pc in zip(LEFT_COUNTS, RIGHT_COUNTS, NUMPAD_COUNTS):
        out.extend(range(left, left + lc))
        out.extend(range(right, right + rc))
        out.extend(range(pad, pad + pc))
        left += lc
        right += rc
        pad += pc
    return out


TRANSFORM = transform_positions()

# The default layer, in the same visual order as TRANSFORM. Row boundaries
# follow LEFT_COUNTS/RIGHT_COUNTS/NUMPAD_COUNTS.
DEFAULT_LAYER = [
    # Function row
    "kp ESC", "kp F1", "kp F2", "kp F3", "kp F4", "kp F5", "kp F6",
    "kp F7", "kp F8", "kp F9", "kp F10", "kp F11", "kp F12", "kp PSCRN", "kp HOME",
    "kp KP_NUM", "kp F3", "kp F4", "kp F7",
    # Number row
    "kp GRAVE", "kp N1", "kp N2", "kp N3", "kp N4", "kp N5", "kp N6",
    "kp N7", "kp N8", "kp N9", "kp N0", "kp MINUS", "kp EQUAL", "kp BSPC", "kp PG_UP",
    "kp ESC", "kp KP_DIVIDE", "kp KP_MULTIPLY", "kp KP_MINUS",
    # Tab row
    "kp TAB", "kp Q", "kp W", "kp E", "kp R", "kp T",
    "kp Y", "kp U", "kp I", "kp O", "kp P", "kp LBKT", "kp RBKT", "kp BSLH",
    "kp KP_N7", "kp KP_N8", "kp KP_N9", "kp KP_PLUS",
    # Home row
    "kp CAPS", "kp A", "kp S", "kp D", "kp F", "kp G",
    "kp H", "kp J", "kp K", "kp L", "kp SEMI", "kp SQT", "kp RET", "kp DEL",
    "kp KP_N4", "kp KP_N5", "kp KP_N6",
    # Shift row
    "kp LSHFT", "kp Z", "kp X", "kp C", "kp V", "kp B",
    "kp N", "kp M", "kp COMMA", "kp DOT", "kp FSLH", "kp RSHFT", "kp UP", "kp PG_DN",
    "kp KP_N1", "kp KP_N2", "kp KP_N3", "kp KP_ENTER",
    # Bottom row, Mac legends: Fn / Control / Option / Command
    "mo 1", "kp LCTRL", "kp LALT", "kp LGUI", "kp SPACE",
    "kp SPACE", "kp RGUI", "mo 1", "kp RALT", "kp LEFT", "kp DOWN", "kp RIGHT",
    "kp KP_N0", "kp KP_DOT",
]

# Key-input bus and scan timing. Fast mode is the speed NocFree documents for
# the factory firmware on this bus; the scan periods are what that speed pays
# for. See docs/architecture.md, "Timing".
BUS_SPEEDS = {"I2C_BITRATE_STANDARD": 100_000, "I2C_BITRATE_FAST": 400_000}
BUS_HZ = 400_000
ACTIVE_SCAN_MS = 2
IDLE_POLL_MS = 5

# Flash geometry, from the two public Adafruit linker scripts. See
# docs/architecture.md for the citation.
PARTITIONS = {
    "sd_partition": (0x00000000, 0x00027000),
    "code_partition": (0x00027000, 0x0003E000),
    "storage_partition": (0x00065000, 0x00008000),
    "boot_partition": (0x00074000, 0x0000C000),
}
READ_ONLY_PARTITIONS = {"sd_partition", "boot_partition"}
FACTORY_FILESYSTEM = (0x0006D000, 0x00074000)
FLASH_END = 0x00080000
