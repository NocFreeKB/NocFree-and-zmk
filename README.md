# NocFree Keyboard ZMK Porting Guide

This document is intended for community members developing ZMK support for NocFree nRF52833 split keyboards. It provides the hardware interfaces and porting information required for community development. ZMK-related code is implemented and maintained by the community; NocFree does not provide official ZMK firmware or guarantee compatibility.

Publishing hardware compatibility information on this page does not make the NocFree keyboard hardware open source. Keyboard schematics, PCB designs, mechanical designs, manufacturing materials, and factory firmware are not open-source parts of this project.

## 1. Disclaimer and No-Warranty Notice

Flashing third-party or self-compiled open-source firmware is an unofficial modification performed at the user's own discretion. It may prevent the device from booting, cause key or wireless functions to fail, erase configuration or pairing data, increase power consumption, or permanently damage the hardware.

Before proceeding, make sure you understand and accept the following:

- Back up the factory firmware, configuration, and pairing data, and prepare a working recovery method.
- Incorrect firmware, devicetree, pin, power-control, or bootloader configuration may render the device unusable.
- After flashing open-source firmware, the factory update tool, web configurator, 2.4 GHz receiver, and other companion features may no longer be compatible.
- The operator assumes all responsibility for device failure, data loss, personal injury, or property damage caused by flashing, modifying, or using unofficial firmware.
- No express or implied warranty is provided for the condition, functionality, stability, compatibility, or recoverability of hardware or software after flashing open-source firmware. No warranty service, returns, replacements, or free technical support are provided.
- Where local law grants mandatory consumer rights, those rights take precedence.

**By proceeding, you acknowledge these risks and accept full responsibility.**

## 2. Project Links

- GitHub repository: <https://github.com/NocFreeKB/NocFree-and-zmk>
- ZMK documentation: <https://zmk.dev/docs>

## 3. Hardware Architecture

The keyboard uses a split design. The current factory firmware implements the following wireless links:

```text
Right nRF52833 internal RADIO ── ESB ──┐
                                       ├── Left external nRF24L01 ── Left controller
Numpad nRF52833 internal RADIO ── ESB ─┘                │
                                                        ├── USB HID ── Computer
                                                        ├── BLE HID ── Computer / mobile device
                                                        └── Left nRF52833 internal RADIO
                                                                │
                                                                └── ESB ── USB receiver
```

In the current code, the external nRF24L01 on the left half connects to the controller over SPI. It primarily receives key data from the right half and numpad and sends backlight control data back to them. The right half and numpad do not drive an external nRF24L01 over SPI; they use their own nRF52833 internal RADIO peripherals and an ESB protocol configured with nRF24-compatible on-air parameters.

When sending HID reports to the USB receiver, the current code uses the left nRF52833 internal RADIO on a channel separated from the split-keyboard link. The external nRF24L01 on the left half therefore should not be described as the USB receiver communication radio.

For a ZMK port, the recommended approach is to replace the factory ESB split link with standard ZMK BLE split communication. Retaining the factory right-half ESB link or USB receiver requires separate ports of the relevant drivers, pairing data, and proprietary protocol; standard ZMK configuration alone is not sufficient.

The keys are not wired directly as row and column GPIOs on the nRF52833. Each half uses multiple PCA9555 devices over I²C as key inputs. The PCA9555 `INT` signal connects to the controller and wakes it when a key state changes. Standard layouts read 6 rows × 8 bits on each half; the KR layout reads one additional row. Unpopulated matrix positions should be ignored by the ZMK transform or key-position mapping.

## 4. Pins Required for ZMK Porting

> `D0` through `D16` in the following tables are logical aliases from this project's Arduino board package. ZMK/Zephyr devicetrees must use the corresponding nRF GPIOs. The left and right aliases use different mappings and are not interchangeable.

### 4.1 Left Controller

| Function | Arduino alias | nRF52833 GPIO | ZMK porting notes |
|---|---:|---:|---|
| PCA9555 interrupt | D1 | P0.31 | Active low; input pull-up; may be used as a wake source |
| I²C SDA | D6 | P0.11 | PCA9555 bus; factory firmware uses 400 kHz |
| I²C SCL | D7 | P1.09 | PCA9555 bus; factory firmware uses 400 kHz |
| BLE position detect | D2 | P0.15 | Input pull-up; low when the switch is in the BLE position; optional in ZMK |
| 2.4 GHz position detect | D3 | P0.17 | Input pull-up; low when the switch is in the 2.4 GHz position; optional in ZMK |
| Red charge/low-battery indicator | D4 | P0.09 | Shared with charge status; factory firmware emulates open-drain control and releases the pin while USB-powered |
| Keyboard backlight | D5 | P0.20 | PWM/GPIO output; the current brightness API drives the pin low at endpoint value `255`; verify the physical on/off polarity after porting |
| Blue status LED | D0 | P0.10 | Direct GPIO control; board definition uses active low |
| Battery ADC | D15 | P0.04 | Reads the battery divider |
| Battery-divider enable | D16 | P0.05 | High enables the divider; disable it after sampling to reduce power consumption |

### 4.2 Right Controller

| Function | Arduino alias | nRF52833 GPIO | ZMK porting notes |
|---|---:|---:|---|
| PCA9555 interrupt | D0 | P0.05 | Active low; input pull-up; may be used as a wake source |
| I²C SDA | D6 | P0.11 | PCA9555 bus |
| I²C SCL | D7 | P1.09 | PCA9555 bus |
| Red charge/low-battery indicator | D4 | P0.17 | Shared with charge status; factory firmware emulates open-drain control and releases the pin while USB-powered |
| Keyboard backlight | D5 | P0.20 | PWM/GPIO output; the current brightness API drives the pin low at endpoint value `255`; verify the physical on/off polarity after porting |
| Battery ADC | D15 | P0.04 | Reads the battery divider |
| Battery-divider enable | D16 | P0.31 | High enables the divider; disable it after sampling to reduce power consumption |

### 4.3 PCA9555 Key Inputs

| Item | Configuration |
|---|---|
| I²C addresses | `0x20`, `0x22`, `0x24` |
| Standard-layout port order | `0x20/P0`, `0x20/P1`, `0x22/P0`, `0x22/P1`, `0x24/P0`, `0x24/P1` |
| Additional KR-layout port | `0x21/P0` |
| Port direction | Input |
| Factory-firmware polarity | PCA9555 input polarity inversion registers set to `0xFF` |
| Valid bits per port | Normally 8 bits; unpopulated key positions are ignored by the mapping layer |

The ZMK port must enable Zephyr I²C and GPIO-expander support and define PCA9555 devicetree nodes. The exact scanning implementation depends on the bindings and driver capabilities provided by the selected ZMK/Zephyr version. If that version cannot use GPIO-expander pins directly for keyboard scanning, a minimal scanning driver will be required.

### 4.4 Left External nRF24L01 Interface (Optional)

In the current code, these SPI signals are used only by the left external nRF24L01 data path for communication with the right half and numpad. They are not required when using standard ZMK BLE split communication. The factory right-half ESB link uses the nRF52833 internal RADIO and does not use these SPI pins.

| nRF24L01 signal | Left Arduino alias | Left nRF52833 GPIO |
|---|---:|---:|
| SCK | D8 | P0.28 |
| MOSI | D9 | P0.29 |
| MISO | D10 | P0.30 |
| CE | D11 | P0.03 |
| CSN | D12 | P0.02 |

## 5. Battery Measurement Parameters

The current firmware uses a 12-bit ADC. It drives the divider-enable pin high before sampling and converts the measured voltage to battery voltage using a `130/100` scale factor. The port should preserve the enable-on-demand and disable-after-sampling behavior to avoid continuous divider current.

These values come from the current firmware and do not replace a complete schematic. Calibrate the ADC reference voltage, divider ratio, battery curve, charge-state detection, and low-voltage threshold against the target hardware revision.

## 6. Recommended Porting Order

1. Create separate nRF52833 board/shield configurations for the left and right halves. Verify USB, serial, and the firmware recovery path first.
2. Enable I²C, confirm that every PCA9555 address can be detected, then verify the interrupt pins and all key inputs.
3. Configure ZMK split with the left half as central and the right half as peripheral. Complete BLE split input first; a standard ZMK port does not require the external nRF24L01 on the left half.
4. Add battery measurement, status indicators, and backlighting. Calibrate active levels, PWM polarity, and the battery curve on real hardware.
5. Evaluate optional compatibility features such as the mode switch and factory 2.4 GHz receiver last.

## 7. Pre-Flash Checklist

- Verify the device hardware revision, keyboard half, and layout.
- Make sure the ZMK devicetree uses nRF GPIO numbers rather than copying Arduino `D` aliases directly.
- Keep a recoverable bootloader and prepare a known-working recovery firmware image before testing.
- Disable the backlight and optional peripherals during initial testing, then verify idle current, battery measurement, and deep sleep.
- Before connecting the battery, check GPIO levels, power-control signals, and the charging circuit to prevent output contention or a continuously enabled divider.

> The pin mappings in this document were derived from the current Arduino firmware and custom board definitions in this repository. Before releasing ZMK firmware, verify them against the schematic for the relevant hardware revision and confirm them on real hardware with a multimeter or logic analyzer.

## 8. License and Scope

Community-developed ZMK support code and its accompanying documentation are licensed under the [MIT License](https://opensource.org/license/mit). This license applies only to content explicitly identified as community ZMK support. It does not apply to NocFree keyboard hardware, schematics, PCB designs, mechanical designs, manufacturing materials, trademarks, bootloaders, or factory firmware.

Contributors must submit only content they have the right to release under the MIT License. Third-party projects and included third-party content remain subject to their original licenses.
