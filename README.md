# WisprKey BLE Button For Wispr Flow

WisprKey is an ESP32-C3 based one-button BLE keyboard for controlling Wispr Flow on macOS. The hardware button is wired to IO7 and sends `F24`; macOS then uses Karabiner-Elements to turn that key into the Fn/Globe behavior Wispr Flow expects.

The working flow is:

1. ESP32-C3 advertises as `WisprKey BLE`.
2. IO7 button sends `F24`.
3. Karabiner-Elements maps `F24` from the WisprKey device to Fn/Globe or Enter.
4. Wispr Flow uses Fn/Globe as the start/stop dictation shortcut.

Generic BLE keyboards do not have a standard cross-platform Fn key. Apple’s internal Fn/Globe key is vendor-specific, so the reliable path is to send `F24` from firmware and handle the Wispr Flow shortcut in macOS software.

## Hardware

- Board: ESP32-C3
- Button input: `IO7`
- Button wiring: `IO7` to `GND`
- Firmware input mode: internal pull-up enabled
- Pressed state: active-low

## Button Behavior

- Single press: Karabiner sends Fn/Globe to toggle Wispr Flow listening.
- Press again: Karabiner sends Fn/Globe again to stop Wispr Flow listening.
- Double-click: Karabiner sends `Enter`.

This behavior is implemented on macOS, not in firmware. The rule keeps Fn/Globe responsive for Wispr Flow and uses a short double-click window for Enter.

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
idf.py set-target esp32c3
idf.py build
```

## Flash

Put the ESP32-C3 in bootloader mode if needed:

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

If these logs appear, the ESP32-C3 and IO7 wiring are working.

## Pair On macOS

1. Open Bluetooth settings.
2. Pair `WisprKey BLE`.
3. If the HID descriptor changed after a firmware update, remove/forget `WisprKey BLE`, then pair it again.

## macOS Wispr Flow Mapping

### Recommended: Karabiner-Elements

Karabiner-Elements is required for the Wispr Flow shortcut and double-click Enter behavior. Install Karabiner-Elements, then copy:

```sh
mkdir -p ~/.config/karabiner/assets/complex_modifications
cp macos/karabiner-f24-to-fn.json ~/.config/karabiner/assets/complex_modifications/wisprkey-f24-to-fn.json
```

Open Karabiner-Elements and enable the rule named `WisprKey: Wispr Flow toggle and double-click Enter`.

If you previously installed the `hidutil` mapping, remove it because it cannot detect double-click timing:

```sh
hidutil property --set '{"UserKeyMapping":[]}'
launchctl bootout gui/$(id -u) ~/Library/LaunchAgents/com.wisprkey.f24-to-fn.plist 2>/dev/null || true
rm -f ~/Library/LaunchAgents/com.wisprkey.f24-to-fn.plist
```

### Simple fallback: hidutil

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
It does not support double-click Enter.

## Test Wispr Flow Behavior

1. Open Wispr Flow and make sure Fn/Globe is configured as the dictation shortcut.
2. Press the WisprKey button once. Wispr Flow should enter listening mode.
3. Press the WisprKey button again. Wispr Flow should stop listening.
4. Double-click the WisprKey button quickly. macOS should receive `Enter`.

## Troubleshooting

- BLE connects but there is no visible response: plain `F24` usually has no visible macOS action unless mapped.
- GPIO logs do not appear: check that the button shorts IO7 to GND and that the firmware was flashed after the latest build.
- BLE connects but mapping does not work: remove and re-pair `WisprKey BLE` after descriptor changes.
- Karabiner install requires a macOS administrator password because it installs a system extension.

## Current Status

Verified during development:

- ESP-IDF build succeeds.
- ESP32-C3 target build succeeds.
- BLE pairs as `WisprKey BLE`.
- IO7 press/release is detected in firmware logs when wired active-low.
- macOS `hidutil` accepts the F24-to-Fn mapping.
