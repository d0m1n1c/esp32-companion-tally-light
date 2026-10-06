# ESP32 Companion Tally Light

An LVGL-based tally light for [Bitfocus Companion](https://bitfocus.io/companion), built for the
[Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm) round touch display board.

The device connects to Companion over Wi-Fi using the Companion Satellite protocol and shows the
active tally color (program/preview) as a full-screen circle, with a touch-driven on-screen menu for
reconnecting Wi-Fi/Companion, triggering camera buttons, and adjusting brightness. Battery
percentage/charging status is also shown via the onboard AXP2101 PMIC.

## Features

- Full-screen tally circle that reflects Companion's program (red) / preview (green) / idle state
- Touch menu with:
  - Wi-Fi reconnect
  - Companion reconnect
  - 8 quick-access camera buttons
  - Brightness slider
- Battery percentage, icon, and charging indicator
- Wi-Fi SSID/password, Companion host, and device ID persisted to flash (`Preferences`/NVS)
- USB serial console for headless configuration (see below) — no credentials are stored in source

## Hardware

- Waveshare ESP32-S3-Touch-AMOLED-1.8 (SH8601 QSPI AMOLED + FT3168 capacitive touch + AXP2101 PMIC)

## Required libraries

This repo **vendors the exact library sources** used to build the project, under [libraries/](libraries),
because one of them (`GFX_Library_for_Arduino`) contains a local patch required to compile against
newer ESP32 Arduino cores. See [LIBRARIES.md](LIBRARIES.md) for the full breakdown of what's vendored
and how each library was verified against its public upstream release.

| Library | Version | Notes |
|---|---|---|
| [lvgl](https://github.com/lvgl/lvgl) | 8.4.0 | unmodified |
| [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) | 1.6.4 | **locally patched**, see [LIBRARIES.md](LIBRARIES.md) |
| [Arduino_DriveBus](https://github.com/espressif/Arduino_DriveBus) | 1.0.1 | Waveshare/author-distributed |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | 0.2.6 | newer than the tag, unmodified |
| [Adafruit_XCA9554](https://github.com/adafruit/Adafruit_XCA9554) | 1.0.0 | unmodified |
| [Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO) | 1.17.4 | unmodified |
| [base64](https://github.com/Densaugeo/base64_arduino) | 1.3.0 | unmodified |
| ESP32 Arduino core | tested with the Waveshare ESP32-S3 board package (3.3.6+) | |

Also requires the board's `pin_config.h` (included in this repo under `Tally_LVGL/`), which defines
the display/touch/I2S/SD pin mappings for this specific board.

> `lv_conf.h` is included in this repo pre-configured for this project — make sure it's the one
> picked up by the LVGL library (place it alongside the sketch, as done here).

## Setup

1. Open `Tally_LVGL/Tally_LVGL.ino` in the Arduino IDE.
2. Select the appropriate ESP32-S3 board profile for the Waveshare ESP32-S3-Touch-AMOLED-1.8
   (PSRAM enabled, USB CDC on boot enabled).
3. Copy the folders under [libraries/](libraries) into your Arduino sketchbook's `libraries/`
   directory (recommended, to get the exact vendored/patched versions — see
   [LIBRARIES.md](LIBRARIES.md)), or install the libraries listed above via the Library Manager.
4. Flash the sketch.
5. Open the serial monitor at 115200 baud and configure the device:

   ```text
   ssid <network>
   pass <password>
   connect
   host <ip:port>        # Companion Satellite host, e.g. 192.168.1.50:16622
   deviceid <id>          # defaults to "esp32-tally"
   save
   reconnect
   ```

### Serial commands

| Command | Description |
|---|---|
| `ssid <network>` | Set Wi-Fi SSID |
| `pass <password>` | Set Wi-Fi password |
| `connect` | Connect to Wi-Fi |
| `scan` | Scan Wi-Fi networks |
| `host <ip:port>` | Set Companion host (triggers auto-reconnect) |
| `deviceid <id>` | Set device ID (triggers auto-reconnect) |
| `reconnect` | Force reconnect to Companion |
| `save` | Save all settings to flash |
| `status` | Show current status |
| `help` | Show the command list |

## License

MIT — see [LICENSE](LICENSE).
