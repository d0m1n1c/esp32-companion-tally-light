# ESP32 Companion Tally Light (legacy, pre-LVGL)

> **This is the `legacy` branch.** It contains the original, pre-LVGL version of this project
> (`Tally.ino`, built with `Arduino_GFX_Library` directly). The current, actively developed version
> (`Tally_LVGL`, built on LVGL with a touch menu, battery status, etc.) lives on the
> [`main`](../../tree/main) branch — start there unless you specifically want this earlier version.

A tally light for [Bitfocus Companion](https://bitfocus.io/companion), built for the
[Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm) round touch display board.

The device connects to Companion over Wi-Fi using the Companion Satellite protocol and shows the
active tally color as a full-screen color fill, drawn directly with `Arduino_GFX_Library` (no LVGL).

## Hardware

- Waveshare ESP32-S3-Touch-AMOLED-1.8 (SH8601 QSPI AMOLED + FT3168 capacitive touch + AXP2101 PMIC)

## Required libraries

This repo vendors the exact library sources used to build this sketch, under [libraries/](libraries).
See [LIBRARIES.md](LIBRARIES.md) for the full breakdown and verification against upstream releases —
in particular, `GFX_Library_for_Arduino` is **locally patched** for compatibility with newer ESP32
Arduino cores.

| Library | Version | Notes |
|---|---|---|
| [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) | 1.6.4 | **locally patched**, see [LIBRARIES.md](LIBRARIES.md) |
| [Arduino_DriveBus](https://github.com/espressif/Arduino_DriveBus) | 1.0.1 | Waveshare/author-distributed |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | 0.2.6 | newer than the tag, unmodified |
| [Adafruit_XCA9554](https://github.com/adafruit/Adafruit_XCA9554) | 1.0.0 | unmodified |
| [Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO) | 1.17.4 | unmodified |
| [base64](https://github.com/Densaugeo/base64_arduino) | 1.3.0 | unmodified |
| ESP32 Arduino core | tested with the Waveshare ESP32-S3 board package (3.3.6+) | |

Also requires the board's `pin_config.h` (included in this repo under `Tally/`), which defines
the display/touch/I2S/SD pin mappings for this specific board.

## Setup

1. Open `Tally/Tally.ino` in the Arduino IDE.
2. Select the appropriate ESP32-S3 board profile for the Waveshare ESP32-S3-Touch-AMOLED-1.8
   (PSRAM enabled, USB CDC on boot enabled).
3. Copy the folders under [libraries/](libraries) into your Arduino sketchbook's `libraries/`
   directory (recommended, to get the exact vendored/patched versions), or install the libraries
   listed above via the Library Manager.
4. Flash the sketch.
5. Open the serial monitor at 115200 baud to configure Wi-Fi/Companion (see the sketch's serial
   command handler for the exact command set — no credentials are stored in source).

## License

MIT — see [LICENSE](LICENSE).
