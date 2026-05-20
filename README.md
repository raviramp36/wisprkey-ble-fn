# WisprKey BLE Fn Button

WisprKey is ESP32-S3 firmware for a one-button BLE keyboard. The button is wired to GPIO21 and sends a function-key fallback (`F24`) plus an experimental Apple vendor Fn/Globe-style HID report.

The reliable macOS path is:

1. ESP32-S3 advertises as `WisprKey BLE`.
2. GPIO21 button sends `F24`.
3. macOS maps `F24` to Fn/Globe with `hidutil` or Karabiner-Elements.

Generic BLE keyboards do not have a standard cross-platform Fn key. Apple’s internal Fn/Globe key is vendor-specific, so the firmware includes an experimental report, but the documented working path is the F24 mapping.

## Hardware

- Board: ESP32-S3 with native USB Serial/JTAG
- Button input: `GPIO21`
- Button wiring: `GPIO21` to `GND`
- Firmware input mode: internal pull-up enabled
- Pressed state: active-low

## Firmware

Project type: ESP-IDF.

BLE identity:

- Device name: `WisprKey BLE`
- Vendor ID: `0x303A`
- Product ID: `0x4005`

Main firmware files:

- `main/main.c` - BLE HID keyboard, GPIO debounce, F24/Fn report sending
- `main/esp_hid_gap.c` and `main/esp_hid_gap.h` - BLE GAP helper adapted from Espressif's ESP-IDF HID device example
- `sdkconfig.defaults` - Bluetooth and flash defaults

## Build

Install ESP-IDF 5.4 or newer, then:

```sh
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

## Flash

Put the ESP32-S3 in bootloader mode if needed:

1. Hold `BOOT`
2. Tap `RESET`
3. Keep holding `BOOT` for about 2 seconds
4. Release `BOOT`

Then flash:

```sh
source ~/esp/esp-idf/export.sh
idf.py -p /dev/cu.usbmodem1101 flash
```

Use the serial port shown on your machine if it differs:

```sh
ls /dev/cu.*
```

## Monitor And Verify GPIO

```sh
source ~/esp/esp-idf/export.sh
idf.py -p /dev/cu.usbmodem1101 monitor
```

When the button is pressed, logs should show:

```text
wisprkey_ble: Press F24
wisprkey_ble: Release F24
```

If these logs appear, the ESP32-S3 and GPIO21 wiring are working.

## Pair On macOS

1. Open Bluetooth settings.
2. Pair `WisprKey BLE`.
3. If the HID descriptor changed after a firmware update, remove/forget `WisprKey BLE`, then pair it again.

## macOS Fn/Globe Mapping

### Option 1: hidutil

Apply immediately:

```sh
hidutil property --set '{"UserKeyMapping":[{"HIDKeyboardModifierMappingSrc":30064771187,"HIDKeyboardModifierMappingDst":1095216660483}]}'
```

Check the mapping:

```sh
hidutil property --get UserKeyMapping
```

Make it persistent at login:

```sh
cp macos/com.wisprkey.f24-to-fn.plist ~/Library/LaunchAgents/
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.wisprkey.f24-to-fn.plist
launchctl kickstart -k gui/$(id -u)/com.wisprkey.f24-to-fn
```

The `hidutil` mapping is global: it maps any keyboard's F24 to Apple Fn/Globe.

### Option 2: Karabiner-Elements

Install Karabiner-Elements, then copy:

```sh
mkdir -p ~/.config/karabiner/assets/complex_modifications
cp macos/karabiner-f24-to-fn.json ~/.config/karabiner/assets/complex_modifications/
```

Open Karabiner-Elements and enable the rule named `Map WisprKey F24 to Mac Fn/Globe`.

## Test Fn Behavior

Hold the WisprKey button and press `Delete` on the Mac keyboard. If macOS accepts the mapping, this should behave like forward-delete.

## Troubleshooting

- BLE connects but there is no visible response: plain `F24` usually has no visible macOS action unless mapped.
- GPIO logs do not appear: check that the button shorts GPIO21 to GND and that the firmware was flashed after the latest build.
- BLE connects but mapping does not work: remove and re-pair `WisprKey BLE` after descriptor changes.
- Karabiner install requires a macOS administrator password because it installs a system extension.

## Current Status

Verified on an ESP32-S3:

- ESP-IDF build succeeds.
- Flash succeeds over `/dev/cu.usbmodem1101`.
- BLE pairs as `WisprKey BLE`.
- GPIO21 press/release is detected in firmware logs.
- macOS `hidutil` accepts the F24-to-Fn mapping.
