<!-- SPDX-License-Identifier: MIT -->

# Typing and connection reliability

This change fixes specific loss and recovery paths in the pinned firmware.
Automated checks cover all three compiled images. The user confirmed that all
keys work on the combined firmware. It does not promise lossless typing through
a radio outage.

## What changed

- **A rejected split notification discarded a press or final release.** Retry the same snapshot
  before later snapshots; repair the current state every 100 ms, even without further typing.
- **A full central event queue discarded individual releases.** Queue complete snapshots for
  each peripheral, then derive transitions on the system queue. Disconnect inserts an all-
  released snapshot.
- **Peripheral reboot or central reboot lost held-key state.** Peripheral repair reports
  resynchronize after encryption and notification subscription become available.
- **Scanner boot/read failure could require a reset or leave keys held.** Retry configuration,
  reject partial scans, reset debounce progress and release stale keys at the 100 ms fault
  threshold.
- **Local scan events overflowed the input queue.** Apply backpressure on the dedicated scan
  thread; the system queue remains available to consume events.
- **A modifier/Fn arrived just after the opposite half's letter.** Give ordinary events and
  modifier releases 15 ms of grace; modifier/Fn presses can overtake pending events within that
  window.
- **A failed scan start left a false “already scanning” flag.** Record success only after
  Bluetooth accepts scanning; retry through a one-second watchdog.
- **Advertising temporarily ran out of resources.** Retry instead of waiting for another key or
  power cycle. Peripheral advertising stops retrying when disabled or connected.
- **Discovery/subscription failed on an otherwise connected split.** Reconnect a split
  connection after five seconds without a valid state report.
- **Bluetooth host report submission failed during congestion or encryption setup.** Retain and
  retry keyboard and consumer reports; repair final states and clear inactive hosts.
- **USB endpoint busy, suspend or reset discarded reports.** Queue reports, retry without
  blocking the system queue, restore the transfer semaphore on reset/resume, and collapse
  obsolete reports. Only input requests wakeup.

The normal split interval remains 7.5 ms. Peripheral latency is zero, meaning a
peripheral does not deliberately skip connection intervals. The supervision
timeout is one second: this is Bluetooth's lost-link detection setting, not a
promise that the host reconnects within one second.

The on-air service layout, HID descriptor, flash partitions and saved bonds are
unchanged. Install the matching firmware on **left, right and numpad**: the
central watchdog expects repair reports from both peripherals. Do not leave an
old peripheral image installed alongside the new central.

## Bounds and trade-offs

- A queue preserves event order until it fills or has been pending for 250 ms.
  It then retains the latest complete state. A tap entirely inside that outage
  can disappear; delayed shortcut replay is intentionally suppressed.
- A 100 ms repair timer means a retry opportunity, not guaranteed delivery.
  Scanner release timing also depends on the bus call returning and scheduling.
  A physically wedged bus or radio cannot be repaired by software alone.
- The shortcut grace applies to the base map's plain modifier and momentary
  layer bindings. It adds 15 ms to ordinary key events and can include a
  modifier pressed shortly after a letter. Larger skew and custom hold-tap,
  combo or macro maps need their own validation.
- Repair traffic, zero skipped split intervals and the existing +8 dBm setting
  trade energy for reliability. Battery life and radio range are unmeasured.
- The existing six ordinary key rollover limit remains. Host/output changes
  clear HID state; release held keys before continuing on the new connection.
  Lost host bonds require pairing repair, not an automatic settings wipe.

## Build and automated evidence

The patch applies only to hash-verified copies of the pinned ZMK sources. It
runs through the module's CMake hook in both local and standard ZMK builds.
The original dependency checkout is untouched. See [build.md](build.md).

```sh
./scripts/build-local.sh
NOCFREE_BUILD_DIR=../nocfree-and-zmk-build/build ./tests/run.sh
```

The tests execute production queue, scanner, chord and transport code with
controlled failures. The built-tree run also executes patched scanning and
central receive functions, checks all three resolved configurations and linked
objects, verifies current source fingerprints, and checks UF2 flash bounds.
These are software and compiled-artifact checks, not physical radio tests.

The implementation was checked against the pinned source and upstream
[split configuration](https://zmk.dev/docs/config/split),
[GATT API](https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/api/gatt.html),
and [connection troubleshooting](https://zmk.dev/docs/troubleshooting/connection-issues).

## Verified local build: 2026-09-11

All three roles built successfully with the pinned ZMK commit and the local
Docker build container. The full suite passed **84 tests with no skips**, plus
the standalone C decoding checks. The runtime fault tests also passed with
AddressSanitizer and UndefinedBehaviorSanitizer enabled. Source fingerprints,
patched compilation paths, linked objects, resolved settings, artifact freshness
and UF2 boundaries passed. The original ZMK checkout remains clean.

| Role | Application bytes | 248 KiB slot used |
|---|---:|---:|
| left | 235,060 | 92.6% |
| right | 191,796 | 75.5% |
| numpad | 191,728 | 75.5% |

Exact UF2 SHA-256 hashes for physical acceptance:

- left: `ec394c4c0c6f575845ece2c1e446ea620e073c178dcddd09b33c53a82a948ca4`
- right: `c52a04b405686815e127656cb9355e97a5d1d53f93c6a9c1ac587be577e9e572`
- numpad: `1944ddcb49ae4c6095782529b0714ea6ccb8160e02ee7be706449d75189e6376`

The local outputs are under the existing build workspace's
`build/reliability/<role>/zephyr/zmk.uf2`. The source-only test mode explicitly
skips build-dependent checks; the result above used `NOCFREE_BUILD_DIR` pointing
to these three builds and `NOCFREE_SANITIZE=1`.

The three combined images at commit `317266d` were flashed on 2026-09-11
and matched byte-for-byte on readback. USB enumeration and bootloader recovery
passed on every part. A stale numpad pairing on the left was repaired with a
separate local utility, preserving the computer and right-half pairings, and
the normal images were restored. The user confirmed all keys work on 2026-09-13.
The device-specific repair utility is not part of the firmware change.

## Physical acceptance

Use a disposable text document and a key-event viewer that displays both presses
and releases. Do not test shortcuts in a terminal or an unsaved working document.
Record the three UF2 SHA-256 hashes, host OS, USB/Bluetooth output, power source,
part placement and results. Previous images' observations do not accept these
images.

1. **Recovery and installation.** Follow [recovery.md](recovery.md) to confirm
   the bootloader and `0x27000` application start. Install the matching images
   on all three parts. Keep another keyboard available. Stop if a part fails
   to enumerate or becomes warm; record the symptom before proceeding.
2. **Individual keys and typing.** With all parts close together, check all
   105 keys once over USB output and once over Bluetooth output. Each press
   needs exactly one release. Type for five minutes, including fast alternation
   between halves and repeated taps. No stuck modifiers, duplicate or missing
   characters are acceptable under these normal conditions.
3. **Cross-part shortcuts.** Repeat 50 times each: left Shift + right Y,
   right Shift + left T, left Command + right-arrow, and right Command + left A.
   Test both Fn keys with an opposite-half function binding, such as left Fn
   with right F10 (mute), and right Fn with left 1 (profile selection; return
   to the paired profile afterward). Test left Shift + numpad 1 in the event
   viewer. Press the modifier slightly before the other key, release in both
   orders, then repeat with near-simultaneous presses. Watch for stray ordinary
   keys, the wrong action or a lingering modifier.
4. **Interference and recovery.** Start at the usual desk placement. Hold a
   right-half key or modifier, move that half far enough away to lose the
   split link, and release the key while it is disconnected. Bring it back
   without touching another key. Repeat with the numpad and with Bluetooth
   host output. The stale key must clear and normal typing must return without
   resetting or clearing bonds. Then try brief obstructions while typing.
   Missing taps during complete disconnection are expected; persistent held
   keys or a connection that needs manual reset are failures.
5. **Power cycles.** Repeat ten times for each part independently, including
   powering off a peripheral while its key is held and starting the central
   last. Release all physical keys before checking the viewer. Both peripherals
   must rejoin and normal typing must work without pairing again. Record actual
   recovery time; stop a trial and record failure if it has not recovered within
   15 seconds of all parts being powered and within range.
6. **Computer sleep.** Run ten sleep/wake cycles over Bluetooth and ten over
   USB, with some sleeps longer than one minute. Include sleep during a held
   key, release it during sleep, then wake using the computer. Confirm no key
   is held and both halves plus numpad type correctly. Also try keyboard wake
   where the host permits it. Stop and record failure if normal typing has not
   returned within 15 seconds after the computer is fully awake. The computer
   may prohibit keyboard wake; that is separate from reconnecting after wake.
7. **Output switching and endurance.** While holding a modifier, switch between
   USB and Bluetooth and between paired profiles. The old host must receive a
   release; release and repress keys on the new host. Finally use the keyboard
   normally for at least an hour, including battery-powered peripherals. Record
   any repeat, omission, lag or manual recovery, plus approximate battery change.

Key operation is user-confirmed on the combined images identified above.
The broader reliability experiments described here have no recorded results.
