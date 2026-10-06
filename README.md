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

Install these via the Arduino Library Manager / Boards Manager (versions this project was built and
tested against):

| Library | Version |
|---|---|
| [lvgl](https://github.com/lvgl/lvgl) | 8.4.0 |
| [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) | 1.6.4 |
| [Arduino_DriveBus](https://github.com/espressif/Arduino_DriveBus) | 1.0.1 |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | 0.2.6 |
| ESP32 Arduino core | tested with the Waveshare ESP32-S3 board package |

Also requires the board's `pin_config.h` (included in this repo under `Tally_LVGL/`), which defines
the display/touch/I2S/SD pin mappings for this specific board.

> `lv_conf.h` is included in this repo pre-configured for this project — make sure it's the one
> picked up by the LVGL library (place it alongside the sketch, as done here).

## Setup

1. Open `Tally_LVGL/Tally_LVGL.ino` in the Arduino IDE.
2. Select the appropriate ESP32-S3 board profile for the Waveshare ESP32-S3-Touch-AMOLED-1.8
   (PSRAM enabled, USB CDC on boot enabled).
3. Install the libraries listed above.
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
