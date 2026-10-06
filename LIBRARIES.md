# Vendored libraries

This repo vendors the exact Arduino library sources used to build `Tally_LVGL`, taken from the
local Arduino `libraries/` sketchbook folder that was actually used to compile and flash the device.
They're included (rather than just listed as dependencies) because at least one of them contains a
local patch that is required for the project to compile — see below.

To keep the repo small, non-essential bulk (bundled examples, demos, datasheets, docs, test suites)
was stripped from each library; only the source needed to build is kept. `SensorLib`, which was
present in the original Arduino libraries folder but is not referenced anywhere by `Tally_LVGL.ino`,
was left out entirely.

| Library | `library.properties` version | Path |
|---|---|---|
| lvgl | 8.4.0 | `libraries/lvgl` |
| GFX Library for Arduino | 1.6.4 (patched, see below) | `libraries/GFX_Library_for_Arduino` |
| Arduino_DriveBus | 1.0.1 | `libraries/Arduino_DriveBus` |
| XPowersLib | 0.2.6 (header) | `libraries/XPowersLib` |
| Adafruit_XCA9554 | 1.0.0 | `libraries/Adafruit_XCA9554` |
| Adafruit_BusIO | 1.17.4 | `libraries/Adafruit_BusIO` |
| base64 | 1.3.0 | `libraries/base64` |

## Verification against upstream

Each vendored library was diffed against the matching upstream release to check for local
modifications:

- **GFX Library for Arduino — locally patched.** `src/databus/Arduino_ESP32SPI.cpp` and
  `Arduino_ESP32SPIDMA.cpp` differ from the official [v1.6.4 release](https://github.com/moononournation/Arduino_GFX/releases/tag/v1.6.4).
  Upstream later shipped an official, version-gated fix for the same problem in
  [commit `9363dd9`](https://github.com/moononournation/Arduino_GFX/commit/9363dd9747f9667e896b906ca3fb254d39f136b4)
  ("Fix build error with ESP32 Arduino core 3.3.6" — `spiFrequencyToClockDiv()` gained a new
  `spi_t*` parameter in ESP32 Arduino core 3.3.6). The vendored copy here applies the same idea but
  unconditionally (no `#if ESP_ARDUINO_VERSION >= ...` guard), so **this vendored copy only builds
  correctly against ESP32 Arduino core 3.3.6+**. If you install the stock v1.6.4 release from the
  Library Manager instead of using the vendored copy, the sketch will fail to build on core 3.3.6+
  with a `spiFrequencyToClockDiv` signature mismatch.
- **lvgl** — byte-for-byte identical to the official [v8.4.0 tag](https://github.com/lvgl/lvgl/tree/v8.4.0) `src/` (aside from the bundled `demos` module, which isn't used by this sketch).
- **Adafruit_BusIO** — identical to the official [1.17.4 tag](https://github.com/adafruit/Adafruit_BusIO/tree/1.17.4).
- **Adafruit_XCA9554** — identical to the upstream [Adafruit_XCA9554](https://github.com/adafruit/Adafruit_XCA9554) repo.
- **XPowersLib** — not a modification, but newer than the `0.2.6` tag: the vendored copy already
  includes a couple of upstream typo-fix commits (e.g. `isEanbleBackupCharger` →
  `isEnableBackupCharger`) that landed on `main` after the `v0.2.6` tag was cut, so the
  `library.properties` version string is stale relative to the actual content.
- **Arduino_DriveBus** — Waveshare/author-distributed library; no independent public upstream
  repository could be located to diff against, so it's vendored as-is from the working install.
- **base64** — small single-purpose library, vendored as-is.

## Using these instead of the Library Manager

If you want to reproduce the exact build, copy (or symlink) the folders under [libraries/](libraries)
into your Arduino sketchbook's `libraries/` directory (replacing any Library-Manager-installed
copies of the same name), rather than installing these from the Library Manager — in particular for
`GFX_Library_for_Arduino`, to pick up the local patch described above.
