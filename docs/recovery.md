<!-- SPDX-License-Identifier: MIT -->

# Recovery

Read NocFree's disclaimer in the [repository README](../README.md) first.
Flashing community firmware is an unofficial modification carried out at your
own risk.

## What this firmware preserves

The application is linked and flashed only into `0x27000..0x64FFF`, with its
settings in `0x65000..0x6CFFF`. The SoftDevice region, the factory filesystem at
`0x6D000..0x73FFF` and the bootloader region at `0x74000..0x7FFFF` are declared
read-only in the devicetree and are never written. The vendor rollback path is
intended to stay intact.

Confirm the layout before you flash anything: put the half into its bootloader,
open the mass-storage volume that appears, and read `INFO_UF2.TXT`. If the
reported application start address is not `0x27000`, **stop** — this firmware's
partition map does not describe your device, and flashing it could overwrite
something that is not the application.

## Getting into the bootloader

Two routes, in order of preference.

**1. 1200-baud touch (both halves).** Both images expose a USB CDC serial
interface. Opening it at 1200 baud requests a warm reset into the preserved UF2
bootloader. This is the same convention Arduino and Adafruit tooling use.

```sh
# macOS / Linux, after identifying the correct port
stty -f /dev/tty.usbmodemXXXX 1200      # macOS
stty -F /dev/ttyACMX 1200               # Linux
```

**2. `Fn`+`Delete` (right half, split link required).** ZMK reset behaviours
act on the half whose key triggered them, and `Delete` is a right-half key, so
this reboots the **right** half into its bootloader — but only while that half
is connected to the left over the split link, because the left half runs the
keymap and forwards the behaviour. A right half that cannot pair — the main
recovery scenario — must use the 1200-baud touch, which is exactly why it
carries the CDC interface. The left half has no bootloader key at all; recover
it with the touch.

If your hardware exposes a reset control, the Adafruit nRF52 bootloader also
supports its usual double-tap entry. This port has not verified whether that
control is accessible on these halves; do not rely on it.

## Order of operations

Flash the **left** half first, and prove recovery on it before touching the
right.

The left half presents USB HID, so it is the only half whose scanner, keymap and
boot behaviour you can verify on its own. More importantly, it is where you can
confirm that 1200-baud touch actually returns a half to the bootloader. Once
that is demonstrated, flashing the right half — whose only recovery path that
does not depend on a working split link is that same mechanism — is a known
quantity rather than a bet.

1. Flash the left half.
2. Verify it boots, enumerates, and types over USB.
3. Trigger 1200-baud touch and confirm the bootloader volume returns.
4. Re-flash the left half, then flash the right.

## Stop conditions

Stop and reassess if any of these occur.

- `INFO_UF2.TXT` reports an application start address other than `0x27000`.
- The bootloader volume does not appear, or a different device appears than the
  one you were targeting.
- A flashed half does not enumerate over USB at all.
- Any key reports as continuously held.
- A half becomes noticeably warm.
- You no longer have a way back into the bootloader on either half.

## Returning to factory firmware

Obtain the correct role-specific factory image through NocFree's official
channel. This repository does not redistribute vendor firmware and cannot
verify it for you.

This firmware leaves unused bytes in the tail of the application region after a
rollback. They are unreachable from the factory application, and leaving them is
preferable to writing over the factory filesystem.
