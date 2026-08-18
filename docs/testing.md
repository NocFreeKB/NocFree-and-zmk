<!-- SPDX-License-Identifier: MIT -->

# Testing

## Automated

```sh
./tests/run.sh
```

Runs with no toolchain and no hardware:

**`tests/test_kscan_scan.c`** — the scanner's decoding logic, compiled from the
same header the firmware uses: register map, port-word layout, active-low
decoding for every bit, and the debounce-credit rule.

**`tests/test_board_definition.py`** — the exact 37/47 key-input lists,
exclusion of every unpopulated expander bit, the transform and its column
offset, keymap coverage and content, flash partition bounds, bus configuration,
split roles, clock source, and module metadata.

**`tests/test_repository_hygiene.py`** — SPDX headers, licence integrity,
formatting, and the absence of private paths, identifiers, binaries or build
output.

**`tests/test_artifacts.py`** — built images, when a build directory is present:
board role, scanner inclusion, and that every flashed byte lands inside the code
partition. CI runs these against a fresh container build; locally they need
`NOCFREE_BUILD_DIR` (below), and `run.sh` fails rather than skipping if that
variable points at something that is not a build tree.

`tests/ansi_spec.py` is the single source of truth for the key map. Changing the
board means changing that file too.

To include the artifact checks, build first and point the suite at the
workspace:

```sh
./scripts/build-local.sh
NOCFREE_BUILD_DIR=../nocfree-and-zmk-build/build ./tests/run.sh
```

## Physical

Automated tests cannot establish that a key is wired where the devicetree says
it is. Before trusting a build on real hardware, work through at least:

1. **Recovery first.** Confirm you can reach the bootloader on both halves and
   that `INFO_UF2.TXT` reports an application start of `0x27000`. See
   [recovery.md](recovery.md).
2. **Boot.** Each half enumerates over USB after flashing and does not become
   warm.
3. **Key inputs.** Every one of the 84 positions produces exactly one press and
   one release. No position is aliased, repeated, inverted, mislocated or stuck.
   Check the halves separately before pairing them.
4. **Warm reset.** Reset each half without removing power and confirm no key
   reports as held. This is the case the scanner's polarity verification exists
   to prevent.
5. **Split.** The halves pair, the right half's keys arrive at the host, and a
   cross-half chord such as left `Shift` + right `Y` produces the right result.
6. **Reconnect.** Power-cycle the right half and confirm it rejoins and that no
   key is left latched.

Record what you actually observed, against the exact images you tested. A
firmware change invalidates a previous physical result.
