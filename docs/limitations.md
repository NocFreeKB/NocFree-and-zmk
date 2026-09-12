<!-- SPDX-License-Identifier: MIT -->

# Limitations

This is a baseline: an ANSI left/right keyboard plus its 21-key numpad over
Bluetooth, and nothing else. Everything below is deliberately absent.

## Not implemented

| | Why |
|---|---|
| Standalone numpad HID | Split peripheral only. Factory firmware can talk to a host alone; this port cannot. |
| Factory USB receiver, ESB / 2.4 GHz | Needs a proprietary protocol and pairing data ported. |
| Battery reporting | ADC and divider-enable pins unverified; the divider must never be left on. |
| Backlight | Needs a verified PWM polarity. Driving it wrong is a hardware risk. |
| Status LEDs, charge indicator | Same: unverified output pins and polarity. |
| Mode switch | The left half's three-position switch has no verified electrical role. |
| ZMK Studio | Requires per-key physical geometry, which this port has not measured. |
| Deep sleep / soft off | Needs a wake source; the expander `INT` line is unused. |
| Gaming / low-latency modes | Out of scope for a baseline. |

No output pin is driven anywhere in this port. Optional and unverified hardware
is left alone rather than configured with a guess.

## Known rough edges

- **Application slot headroom.** The left image currently fills about 91% of
  the 248 KiB code partition and the right about 75%. The numpad image is
  expected to match the right. That is enough for keymap changes, not for a
  large feature. The application region has 280 KiB in total,
  so the code/settings split could be moved — but doing so relocates the
  settings partition and discards saved pairings, so it should be decided before
  people start using this firmware rather than after.
- **Idle current.** Polling keeps the I2C bus busy for roughly 0.5 ms out of
  every 5 ms even when nothing is pressed. Expander-interrupt idle wakeup is
  the fix, and is also what deep sleep would need.
- **Bottom-row modifiers.** The default keymap follows the Mac legends on the
  retail ANSI keycaps (`Fn` / `Control` / `Option` / `Command` from the outside
  in). This is the least certain part of the map. It is a keymap edit only and
  does not affect the electrical mapping.
- **Numpad testing.** The combined numpad and reliability images at commit
  `317266d` were flashed and read back byte-for-byte on all three parts.
  USB enumeration and bootloader recovery passed. After a targeted repair of
  a stale numpad pairing on the left half, the user confirmed normal typing
  and subsequently confirmed that all keys work (2026-09-13). This hardware
  observation is for the combined images, rather than this feature alone.
- **Split reliability.** This port uses ZMK's stock split protocol with no
  code additions, tuned for link margin: every part stays on the more
  sensitive 1M PHY (`CONFIG_ZMK_BLE_EXPERIMENTAL_CONN`) and carries a deeper
  BLE TX pipeline and deeper state queues, because the stock peripheral
  drops a key-state notification the stack refuses to accept. Across a long
  enough fade that drop can still happen — there is no periodic full-state
  repair — so a dropped final notification is corrected by the next key
  event from that half, or by the release-everything cleanup on disconnect,
  rather than immediately. Every part now transmits at the radio's +8 dBm
  maximum (see architecture.md); closing the drop itself is upstream work.
- **Split latency.** The two halves poll on independent schedules, so
  cross-half event ordering can be off by up to one idle poll period plus the
  BLE connection interval. No latency figure is claimed.

## Hardware status

Observed on one ANSI unit, on macOS.

The current sources — +8 dBm transmit power, the 400 kHz bus and the 2 ms /
5 ms scan periods — have not been observed on hardware yet. Everything below
was recorded against earlier images.

With the split-link images (the `feat: harden the split link at desk
distances` commit; both halves' images read back from the bootloader after
flashing and verified byte-for-byte at every written address), on 2026-08-19:

- Informal stress typing with the halves roughly 50 cm apart and objects
  placed between them showed none of the previous symptoms. With the baseline
  images the same unit showed lag, cross-half reordering and occasional stuck
  keys from roughly 30 cm even unobstructed.
- At roughly 80 cm separation with objects between the halves, the link
  became patchy again. Transmit power has since been raised in the sources
  (above); its effect has not been observed.
- Distances are approximate and uninstrumented, from normal desk use.

With the baseline images (the `feat: minimum ANSI left/right ZMK port`
commit):

- Both halves install through the preserved bootloader and boot. The bootloader
  reports `SoftDevice: S140 7.3.0`, which is what puts the application base at
  `0x27000` — so the partition map is confirmed on hardware, not just inferred.
- The right half's installed image was read back from the bootloader and matched
  the built image byte for byte.
- The 1200-baud recovery trigger reaches the bootloader on both halves.
- Every one of the 37 left-half keys was checked individually and reported
  correctly, over USB and over Bluetooth.
- With both halves assembled, the keyboard works over USB and over Bluetooth.
  A key-by-key sweep of all 84 positions has not been recorded.

That is a functional pass for the baseline this port aims at. It is one unit,
one host operating system, and one hardware revision.

## Claims this port does not make

- Battery-powered operation has since been observed in normal use, but only
  informally; the baseline acceptance itself was recorded with both halves on
  USB power, and no battery life figures are claimed.
- Reconnection after a power cycle, and rollback to factory firmware, have not
  been exercised.
- No battery life, latency, idle current, or endurance figures.
- No Windows or Linux compatibility claims.
- No claim about any other unit or hardware revision.
