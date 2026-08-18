<!-- SPDX-License-Identifier: MIT -->

# Architecture

This is a ZMK keyboard module (`zmk-keyboard-nocfree-and`) providing two
onboard-controller boards for the NocFree & ANSI split keyboard.

| | Left | Right |
|---|---|---|
| Board target | `nocfree_and_left/nrf52833/zmk` | `nocfree_and_right/nrf52833/zmk` |
| Split role | Central | Peripheral |
| Host output | BLE HID, USB HID | none |
| Keys | 37 | 47 |
| Keymap positions | 0-36 | 37-83 |

The left half owns the keymap and talks to the computer. The right half scans
its own keys and sends key positions to the left half over ZMK's standard BLE
split link. There is no dongle and no proprietary radio protocol.

## Key scanning

The keys are not an MCU row/column matrix. Each half has three
PCA9555-compatible 16-bit I2C expanders at `0x20`, `0x22` and `0x24`, and every
key has its own expander input. The six logical scan rows are the six 8-bit
input ports, read in the order NocFree publishes: `0x20/P0`, `0x20/P1`,
`0x22/P0`, `0x22/P1`, `0x24/P0`, `0x24/P1`.

A standard ANSI board split between `T` and `Y` gives 15/15/14/14/14/12 keys per
row, so the left half populates 7/7/6/6/6/5 inputs and the right half
8/8/8/8/8/7. The remaining expander bits are unpopulated positions and are
simply absent from each board's `key-inputs` list.

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

Both problems live in the exact code path a generic solution would depend on.
Rather than carry downstream patches against Zephyr, which a normal ZMK user
building through GitHub Actions cannot apply, this module drives the three
expanders over I2C directly. **This contribution patches neither ZMK nor
Zephyr.**

The scanner (`drivers/kscan/kscan_pca9555.c`, `nocfree,kscan-pca9555`):

- writes `0x00 0x00` to the polarity registers and `0xFF 0xFF` to the
  configuration registers at init, then **reads both back and fails
  initialisation if either does not match**;
- refuses to report anything unless that verification passed, so a scanner that
  cannot vouch for its input stays silent rather than emitting stuck keys;
- reads each expander once per scan with a two-byte port-pair read, so a scan
  costs one I2C transaction per expander regardless of key count;
- reports through ZMK's normal KSCAN interface, so it composes with
  `zmk,matrix-transform`, `zmk,physical-layout` and BLE split with no other
  changes.

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

The cost is idle current: the bus is active for roughly 1.5 ms out of every
10 ms while no key is held. The benefit is that key reporting does not depend on
any unverified signal. Interrupt-driven idle wakeup is the obvious follow-up
once someone has measured the line, and it is a prerequisite for useful deep
sleep.

### Timing

Scan periods must exceed the bus transfer time or an absolute scan deadline
falls permanently into the past and the scan thread never idles.

One two-byte port-pair read is about 48 bit times plus framing:

| Bus speed | Per expander | Three expanders |
|---|---|---|
| 100 kHz | ~0.48 ms | ~1.5 ms |
| 400 kHz | ~0.12 ms | ~0.4 ms |

At 100 kHz the active period is therefore 3 ms rather than ZMK's 1 ms default,
with a 10 ms idle period. The driver also resynchronises its deadline a whole
period ahead if a scan overruns, so a slow bus degrades the scan rate instead of
spinning.

Scanning runs on a dedicated preemptible work queue. The system work queue is
cooperative and is where ZMK builds HID reports and split notifications; a
blocking 1.5 ms bus transfer there would sit directly in front of the key events
the scan just produced.

### I2C bus speed

**This port defaults to 100 kHz standard mode.**

NocFree documents 400 kHz for the factory firmware, and the PCA9555 family is
rated for it. But bus margin is a property of a specific unit's interconnect
capacitance and pull-up values, and this port has not measured it on any
hardware. Any bus that works at 400 kHz also works at 100 kHz; the reverse is
not guaranteed. Standard mode is therefore the setting that is safe whichever
assumption turns out to be right, and it costs only scan rate.

Raising it is one line in `nocfree_and.dtsi` (`I2C_BITRATE_FAST`), after which
`debounce-scan-period-ms` can drop to 1 ms. Do that only after confirming clean
edges on the real bus.

### Position mapping

Both halves share one 84-position `zmk,matrix-transform` describing the whole
keyboard as a single logical row of independent columns. Each half's scanner
reports its own local column index; the right half's board devicetree applies
`col-offset = <37>` so its 47 columns land on global positions 37-83.

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
| Default radio transmit power | Raising it is an unmeasured power and emissions change. |
| No backlight, LED, battery or mode-switch nodes | Each needs an output pin or polarity this port has not verified. |
| 100 kHz I2C | See above. |

Nothing in this port drives an output pin. The only pins it configures at all
are `P0.11` and `P1.09` for I2C, both published by NocFree.

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
