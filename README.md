# EchoScope

*A miniature radar station for the aircraft overhead.*

![Radar modes and highlighted aircraft trails](docs/radar-preview.png)

A live aircraft radar for the **VIEWE UEDX46460015-MD50ET** rotary touchscreen: an ESP32-S3 with a 1.5-inch round AMOLED, touch input, encoder and push button.

Aircraft data is supplied by [adsb.fi](https://adsb.fi/) over Wi-Fi. No ADS-B receiver is required. A labelled demo is available before configuration.

## Features

- North-up radar centred on your location, with 5 / 10 / 25 / 50 / 100 km ranges.
- Rotate to zoom or select aircraft; a bare knob press toggles the mode.
- Bold, brighter footer text shows what rotation controls.
- Touch an aircraft to see callsign, registration, type, altitude, ground speed, ground track, distance, bearing and position age where available.
- Faint green flight paths, with the selected path highlighted in amber above other paths.
- N/E/S/W compass labels and a decorative radar sweep.
- Local Wi-Fi/location setup, saved settings, stale-data indication and reconnection handling.

Latest firmware: **0.2.3**. It includes the two-transitions-per-detent adjustment for one action per physical click.

## Hardware status

The user has confirmed working display, Wi-Fi, live aircraft retrieval, touch/details and rotary input on the VIEWE device. Version 0.1.3 fixed the observed TLS stalls by separating network and display work across CPU cores. Versions 0.2.1–0.2.2 compile successfully; the latest detent adjustment, selected trail styling and compass additions still need confirmation on the physical device. Host interaction tests and actual LVGL screen rendering are documented in [VALIDATION.md](VALIDATION.md).

## Controls

| Input | Radar view | Flight details |
|---|---|---|
| Rotate | Zoom or cycle visible aircraft, depending on mode | Cycle visible aircraft |
| Press without touching the screen | Toggle zoom / aircraft selection | Return to radar, preserving the mode |
| Touch an aircraft | Select it and open details | Tap to return to radar |
| Touch blank radar space | Open the highlighted aircraft | Tap to return to radar |
| Tap bottom range / aircraft count | Select zoom / aircraft-selection mode | — |
| Hold knob 1.5 seconds | Open Wi-Fi/location setup | Open Wi-Fi/location setup |

The active footer section is bold and brighter: **50 km** for zoom, or **2 aircraft** for selection. Touch and mechanical-click events are combined into one gesture, so pressing the screen does not immediately undo the touch action. A bare click is deferred by 180 ms to allow touch detection.

## First installation

The target has an **ESP32-S3, 16 MB flash and 8 MB OPI PSRAM**. Use the existing ribbon/USB adapter. Its jumper functions have not been verified here.

Run `make setup` to install the local tools. If desired, preserve the factory firmware before uploading:

```sh
.tools/venv/bin/python -m esptool --chip esp32s3 --port /dev/ttyACM0 read_flash 0x0 0x1000000 factory-backup.bin
```

Build with `make firmware` or download the release assets first. With the device in upload mode, write the merged image at **0x0**:

```sh
.tools/venv/bin/python -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 dist/echoscope-merged.bin
```

Use the serial port your device exposes. The commands use esptool 4.x syntax. Restart normally without holding BOOT after flashing.

1. Join **EchoScope-Setup** using the randomly generated Wi-Fi password displayed on the knob.
2. Open **http://192.168.4.1**.
3. Enter your 2.4 GHz Wi-Fi credentials and the decimal latitude/longitude at the centre of your radar. West and south use negative coordinates.
4. Save. The device connects, synchronises its clock and starts retrieving aircraft.

Before saving settings, press/tap to explore the labelled demo, then hold the knob to return to setup. Demo aircraft are never substituted for live aircraft after setup. Credentials and location stay in device storage; location is sent to adsb.fi to request nearby aircraft. The setup form never displays the saved Wi-Fi password. A blank password preserves it when retaining the same SSID.

Setup closes five minutes after being opened if Wi-Fi is connected. A failed connection leaves recovery setup available. Holding the knob reopens it. Range and rotation mode reset on reboot; Wi-Fi and location persist.

## Updating an existing installation

Flash the application-only image at **0x10000**, retaining Wi-Fi and location:

```sh
.tools/venv/bin/python -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash 0x10000 dist/echoscope-app-0.2.3.bin
```

This offset assumes an existing EchoScope installation with this project's partition layout. The merged image spans the settings partition; use it for first installation, not a settings-preserving update. Firmware checksums are in `dist/SHA256SUMS`.

## Build from source

Prerequisites: Linux/macOS, Python 3.10 or newer with `venv` and pip, GNU Make, Git and a C++17 compiler (g++/clang++ for host tests). On Windows, use WSL for builds or PlatformIO directly; USB access needs separate configuration. The Makefile creates its Python environment and PlatformIO toolchain under `.tools/` in the project; it does not install packages system-wide.

```sh
git clone https://github.com/MrPsyware/echoscope.git
cd echoscope
make setup
make test
make firmware
```

Firmware images and checksums appear in `dist/`. After entering upload mode:

```sh
make backup PORT=/dev/ttyACM0       # optional: preserves a full 16 MB flash copy
make flash-full PORT=/dev/ttyACM0   # first installation only
# OR, for updates to an existing installation:
make upload PORT=/dev/ttyACM0       # writes only the application, retaining settings
make monitor PORT=/dev/ttyACM0
```

Run `make help` for all commands, including `deps`, `build`, `ports` and `clean`. `clean` keeps downloaded tools and backups. You can override `PYTHON`, `CXX`, `PORT`, `BAUD` and `PLATFORMIO_CORE_DIR`. Use `make -j` only for a single target; operations that share PlatformIO's build directory should not be run simultaneously.

Prebuilt images are available from [GitHub Releases](https://github.com/MrPsyware/echoscope/releases). The older project name was Sky Knob; its NVS storage namespace is intentionally retained for settings compatibility. The setup Wi-Fi is now **EchoScope-Setup**.

The first build downloads the toolchain. Dependencies are pinned in `platformio.ini`: Arduino-ESP32 3.1.1 through PioArduino, Espressif Display Panel 1.0.3 and LVGL 8.4.0, among others.

The supported `BOARD_VIEWE_UEDX46460015_MD50ET` definition is used. Its touch wiring differs from the manufacturer's README table:

- Encoder A/B: GPIO6 / GPIO5. Push button: GPIO0. Two transitions per detent.
- Driver touch configuration: SDA GPIO1 / SCL GPIO3, CST820.
- CO5300 QSPI display: GPIO12 CS, GPIO10 clock, GPIO13/11/14/9 data, GPIO8 reset and GPIO17 power.
- Driver resolution: 472 × 466; the round 466 × 466 interface is centred within it. RGB565 byte swapping is enabled.
- Network/Arduino task: CPU core 0. LVGL: core 1. Render period adapts to leave at least 50 ms after drawing, with a maximum of five frames per second.

## Aircraft and trails

The nearest 64 valid airborne aircraft within 100 km are retained. Rotation skips aircraft outside the current visible range. Selection follows aircraft identity across updates. Unknown fields are shown as unavailable; origin/destination lookups are not implemented.

Trails retain the path while the aircraft remains visible and fresh. They clear when the aircraft disappears, exits the current range (including after zooming), or its position ages past 60 seconds. Re-entry starts a new history. The start of the encounter is preserved while older geometry is simplified when the bounded 192-point history fills. Straight sections are simplified within a 20-metre tolerance. Histories are stored in PSRAM and clipped at the radar boundary.

The sweep is decorative: aircraft are shown at their last reported positions, without extrapolation. Coverage depends on the feed's receivers.

## Networking and diagnostics

Aircraft are requested from `https://opendata.adsb.fi/api/v3/lat/{lat}/lon/{lon}/dist/54`. Polling waits five seconds after a successful request; failures back off up to two minutes. HTTP 429 waits two minutes. An empty successful feed clears the list; a failed request retains the ageing snapshot. Positions dim after 20 seconds and disappear from the radar after 60 seconds.

HTTPS verifies the hostname and certificate using Google Trust Services Roots R1 and R4. A valid clock is required. The pinned TLS client advertises HTTP/1.1 and an ECDHE AES-GCM cipher set. On the first failed feed connection per boot, it tests a verified TLS handshake to `pki.goog`, without sending an HTTP request or location data. Serial logs include network, TLS, task and rendering diagnostics.

The currently pinned Arduino core can emit `Bad file number` messages even while verified TLS and live updates succeed. These messages remain an unresolved client-integration issue; inspect the `[network]` and `[tls]` results rather than assuming every such line represents a failed fetch.

Use the [adsb.fi public API](https://github.com/adsbfi/opendata) in accordance with its personal/non-commercial terms and rate limits. API access is not guaranteed.

## Host tests and CI

`make test` compiles and runs the model/input and JSON tests on the host. They cover projection, range and selection behaviour, touch/button arbitration, history cleanup and clipping, JSON validation and capacity bounds. `make firmware` validates the generated application partition offset before merging images.

GitHub Actions runs setup, host tests and a complete firmware build on pushes and pull requests, and stores firmware as a downloadable workflow artifact. See [VALIDATION.md](VALIDATION.md) for prior checks and hardware limitations.

## Printable enclosure

A vintage radar-station-style 3D-printed enclosure is planned. CAD/STL files and assembly instructions will be added when available; they are not included yet.

## Credits and licence

This is a new application inspired by [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar). Hardware support comes from the [VIEWE examples](https://github.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display) and Espressif libraries.

Original application code is MIT licensed. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md) for component attribution and licences.
