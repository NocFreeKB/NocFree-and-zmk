<!-- SPDX-License-Identifier: MIT -->

# Architecture

This is a ZMK keyboard module (`zmk-keyboard-nocfree-and`) providing three
onboard-controller boards for the NocFree & ANSI split keyboard and its
matching 21-key numpad.

| | Left | Right | Numpad |
|---|---|---|---|
| Board | `nocfree_and_left` | `nocfree_and_right` | `nocfree_and_numpad` |
| SoC variant | `nrf52833/zmk` | `nrf52833/zmk` | `nrf52833/zmk` |
| Split role | Central | Peripheral | Peripheral |
| Host output | BLE HID, USB HID | none | none |
| Keys | 37 | 47 | 21 |
| Keymap positions | 0-36 | 37-83 | 84-104 |

The left half owns the keymap and talks to the computer. The right half and
numpad each scan their own keys and send key positions to the left half over
ZMK's standard BLE split link. There is no dongle and no proprietary radio
protocol. The numpad is a second split peripheral, not a standalone host
keyboard.

## Key scanning

The keys are not an MCU row/column matrix. Each ANSI half has three
PCA9555-compatible 16-bit I2C expanders at `0x20`, `0x22` and `0x24`, and every
key has its own expander input. The six logical scan rows are the six 8-bit
input ports, read in the order NocFree publishes: `0x20/P0`, `0x20/P1`,
`0x22/P0`, `0x22/P1`, `0x24/P0`, `0x24/P1`.

A standard ANSI board split between `T` and `Y` gives 15/15/14/14/14/12 keys per
row, so the left half populates 7/7/6/6/6/5 inputs and the right half
8/8/8/8/8/7. The 21-key numpad has visual groups of 4/4/4/3/4/2: top
functions, clear/operators, 789/+, 456, 123/Enter, and 0/decimal. Its populated
inputs are `pca20` pins 0-14 followed by `pca22` pins 0-5. A diagnostic image
on the physical unit configured and verified those two devices, then received
I/O error `-5` from unused address `0x24`. The numpad scanner therefore owns
only `0x20` and `0x22`; making the non-responsive address mandatory caused the
entire scanner to fail closed. The remaining expander bits are absent from the
board's `key-inputs` list.

The numpad's addresses, I2C pins and populated-input order were cross-checked
against a separately audited, physically accepted numpad configuration. An
earlier six-port inference was rejected after it paired successfully but
produced no input on hardware.

Key inputs are active low: a pressed key pulls its input to ground against the
part's internal pull-up.

### Why this module ships its own scanner

ZMK's stock `zmk,kscan-gpio-direct` driver is close to what this hardware needs
— it already collapses consecutive pins on one controller into a single
`gpio_port_get()` — but it cannot be used here, for two independent reasons in
the Zephyr revision ZMK pins (`zmkfirmware/zephyr` `v4.1.0+zmk-fixes`).

**1. `gpio_port_get()` on a PCA9555 returns uninitialised data.** In
`drivers/gpio/gpio_pca_series.c`, `gpio_pca_series_port_read_standard()` ends
its non-interrupt path with

```c
value = sys_le32_to_cpu(input_data);   /* assigns the pointer, not *value */
```

The caller's output parameter is never written, so the value is left
uninitialised. Upstream Zephyr fixed this in commit `8409e425b385`
("drivers: gpio: pca series: dereference pointer in assignment", 2025-05-11),
which landed after the v4.1.0 release ZMK's Zephyr fork is based on. The fix is
therefore not present in the revision ZMK builds against today, and any
port-based read of these expanders returns garbage.

**2. The polarity-inversion registers are unreachable.** The same driver omits
registers `0x04`/`0x05` entirely (`polarity_inversion (unused, omitted)` in
every part table), and its software reset does not write them. Those registers
survive a warm MCU reset while the expanders stay powered. Firmware that leaves
them inverting — which NocFree documents the factory firmware as doing — would
combine with active-low inputs to make every released key read as pressed.

Both problems live in the generic GPIO path. This module instead reads the
expanders directly over I2C. Zephyr is unchanged. The reliability extension
patches verified copies of selected ZMK sources at build time; see
[reliability.md](reliability.md).

The scanner (`drivers/kscan/kscan_pca9555.c`, `nocfree,kscan-pca9555`):

- writes zero to the polarity registers and all ones to the configuration
  registers, then reads both back before accepting any input;
- retries verification on its scan queue, including after an unavailable
  expander at boot, without requiring a power cycle;
- reads all populated expanders before updating any debouncer, so a partial
  scan cannot produce a mixture of current and stale key events;
- resets debounce progress on errors, holds existing keys across short faults,
  and releases them when an outage reaches 100 ms; recovery rechecks registers
  and resumes normal debouncing;
- reports through ZMK's KSCAN interface, with backpressure on the dedicated
  scan thread instead of dropping an event when the receiving queue is full.

### Polling, not expander interrupts

NocFree publishes a PCA9555 `INT` line on each half (left `P0.31`, right
`P0.05`). This port does not use it, and polls instead.

- Debouncing needs repeated sampling regardless. An interrupt can only replace
  the *idle* poll; the active scan loop still has to exist.
- The PCA9555 clears `INT` when the input port is read. A key that changes
  during that read is not latched, so a purely edge-driven scanner can drop the
  event and leave a key stuck. Avoiding that requires a periodic sweep anyway.
- Zephyr's expander interrupt support is in the same driver whose port-read path
  is broken above.
- Correct interrupt behaviour depends on the `INT` line's electrical
  characteristics on real hardware, which this port has not measured.

The cost is idle current: the bus is active for roughly 0.5 ms out of every
5 ms on an ANSI half and roughly 0.3 ms on the numpad while no key is held. The
benefit is that key reporting does not depend on any unverified signal.
Interrupt-driven idle wakeup is the obvious follow-up once someone has measured
the line, and it is a prerequisite for useful deep sleep.

### Timing

Scan periods must exceed the bus transfer time or an absolute scan deadline
falls permanently into the past and the scan thread never idles.

One two-byte port-pair read is about 48 bit times plus framing:

| Bus speed | Per expander | Numpad (two) | ANSI half (three) |
|---|---|---|---|
| 400 kHz | ~0.12 ms | ~0.3 ms with overhead | ~0.5 ms with overhead |
| 100 kHz | ~0.48 ms | ~1.0 ms | ~1.5 ms |

At 400 kHz the active period is 2 ms and the idle poll 5 ms. A press on one
part is reported after the wait for the next idle poll (0-5 ms), one scan
(~0.5 ms), and the debounce threshold: ZMK's debouncer integrates real elapsed
time and flips on the first scan at which the 5 ms default has accumulated,
which is 6 ms after the press is first seen at a 2 ms period. ZMK's 1 ms
default period would save 1 ms of that while keeping the bus busy for half of
every millisecond that any key is held; 2 ms is the better trade. At 100 kHz
the same scan takes ~1.5 ms and the periods have to widen to 3 ms active /
10 ms idle, which is how this port shipped originally. The driver also
resynchronises its deadline a whole period ahead if a scan overruns, so a slow
bus degrades the scan rate instead of spinning.

Scanning runs on a dedicated preemptible work queue. The system work queue is
cooperative and is where ZMK builds HID reports and split notifications; a
blocking 1.5 ms bus transfer there would sit directly in front of the key events
the scan just produced.

### I2C bus speed

**This port runs the bus in 400 kHz fast mode.**

That is the speed NocFree documents for the factory firmware on this bus
(porting guide, section 4.1), and the PCA9555 family is rated for it. Bus
margin is still a property of a specific unit's interconnect capacitance and
pull-up values, and this port has not put a scope on any hardware; the
evidence is that the board was designed to run at this speed and does so in
its factory firmware.

A bus that does not hold up at 400 kHz fails in one of two visible ways. A
transfer that is not acknowledged or times out is a read error, which the
scanner logs and retries. It releases stale keys after a sustained outage
(100 ms threshold), then rechecks configuration before accepting input. A
corrupted bit that is acknowledged is chatter on one key, which the 5 ms
debounce suppresses unless it persists. If either is seen, set
`I2C_BITRATE_STANDARD` in `nocfree_and.dtsi` and widen the scan periods to
3 ms active / 10 ms idle in each board file; the timing budget above still
covers that configuration, which is how this port shipped originally.

### Position mapping

All three parts share one 105-position `zmk,matrix-transform` describing the
whole keyboard as a single logical row of independent columns. Each scanner
reports its own local column index; the right half applies `col-offset = <37>`
and the numpad `col-offset = <84>` so those columns land on global positions
37-83 and 84-104.

That is what makes the split work: ZMK peripherals send absolute key positions
to the central, so the peripheral's transform has to resolve to global keymap
positions on its own.

The physical layout deliberately has no `keys` array. Those coordinates only
drive ZMK Studio's visual editor, which this baseline does not enable, and the
board's physical key geometry has not been measured by this port. Guessed
geometry would be worse than none.

## Deliberately conservative choices

| Choice | Reason |
|---|---|
| 32.768 kHz from the internal RC oscillator | No crystal is confirmed fitted; selecting an absent one stops BLE. |
| No DC/DC regulator mode | Requires external inductors this port cannot confirm are present. |
| No backlight, LED, battery or mode-switch nodes | Each needs an output pin or polarity this port has not verified. |

Nothing in this port drives an output pin. The only pins it configures at all
are `P0.11` and `P1.09` for I2C, published by NocFree for the halves and now
also exercised successfully against both populated numpad expanders.

## Radio transmit power

Every part transmits at +8 dBm, the nRF52833's maximum, rather than the 0 dBm
default (`CONFIG_BT_CTLR_TX_PWR_PLUS_8`). This is the setting ZMK's own
documentation recommends for unreliable links and notes as also improving the
link between split halves, with a power cost it describes as negligible: a
keyboard's radio transmits for a small fraction of each connection interval.
On the left half it applies to the host link and to both peripheral links; on
the right half and numpad it applies to the link back to the central. Both
directions of the split link gain margin only when every part sets it. It
changes how loudly each part talks, not how it listens — the receive side is
what the 1M PHY choice protects.

What it costs is unmeasured on this hardware: neither the added current draw
nor the radiated field strength has been characterised, and the antenna is
whatever NocFree fitted. The setting is a single Kconfig line per part and is
the first thing to revert if either turns out to matter.

## Flash layout

The application is confined to the region the factory firmware already uses. The
SoftDevice, bootloader and factory filesystem are never written.

```
0x00000..0x26fff  MBR + S140 v7          preserved, marked read-only
0x27000..0x64fff  ZMK application        248 KiB
0x65000..0x6cfff  ZMK settings (NVS)      32 KiB
0x6d000..0x73fff  factory filesystem     preserved, deliberately unmapped
0x74000..0x7ffff  bootloader + metadata  preserved, marked read-only
```

These boundaries come from two public sources, not from inspecting any device:

- `Adafruit_nRF52_Arduino` 1.7.0, `cores/nRF5/linker/nrf52833_s140_v7.ld`:
  `FLASH ORIGIN = 0x27000, LENGTH = 0x6D000 - 0x27000`. The application region
  starts at `0x27000` and ends where the Adafruit `InternalFS` begins.
- `Adafruit_nRF52_Bootloader`, `linker/nrf52833.ld`: bootloader at `0x74000`,
  bootloader config at `0x7D800`, MBR parameters at `0x7E000`, bootloader
  settings at `0x7F000` — the whole `0x74000..0x7FFFF` region is bootloader
  owned.

NocFree's porting guide states the factory firmware is built with an Arduino
board package, which is what makes those two files the right references. **It
remains an inference.** Confirm it against `INFO_UF2.TXT` on the bootloader
drive before flashing; see [recovery.md](recovery.md).
