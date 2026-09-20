# Validation — 20 September 2026

- PlatformIO ESP32-S3 release build: passed with pinned dependencies.
- Firmware application: 1,629,404 bytes; static RAM: 71,204 bytes. These figures do not include runtime heap or PSRAM usage. Canvas allocation is in PSRAM; the LVGL port uses internal DMA buffers.
- Merged flash image generated with esptool 4.8.5 from the build's bootloader, partition table, boot_app0 and application. Flash offsets taken from the PlatformIO environment: 0x0, 0x8000, 0xe000, 0x10000.
- Host model tests with AddressSanitizer and UndefinedBehaviorSanitizer: passed. Projection, date-line crossing, ranges, selection across reorder/removal, trails, stale hit testing and millis rollover.
- Host JSON tests with the same sanitizers: passed. Numeric fields, missing fields, ground aircraft, expired positions, invalid/empty feed and nearest-aircraft capacity limit.
- Native LVGL 8.4.0 rendering of the actual `radar_ui.h`: radar, details and setup inspected. Tap-to-details and tap-to-radar assertions passed. The preview uses labelled sample aircraft, not a screenshot of live data or a physical device.
- adsb.fi nearby-aircraft endpoint: HTTPS request returned HTTP 200 and aircraft JSON from the development computer. Server chain was checked before selecting the GTS Root R4 trust anchor. Device-side networking has not been exercised.
- No USB serial device was present. No flashing was performed.

## Remaining hardware validation

Boot with the factory backup retained if wanted, verify AMOLED orientation/colours, touchscreen alignment, both encoder directions, transitions per detent, button press/hold, Wi-Fi setup and live updates. Check for dropped frames and heap headroom in dense airspace. Unplug Wi-Fi to verify the stale and reconnect screens on the device. The manufacturer's README and driver disagree on touch wiring; the supported driver definition is used.

## Version 0.1.1 — certificate fix

User confirmed first firmware boots, setup works and Wi-Fi connects, but aircraft requests return -1. That transport error alone does not identify the cause. Reproduced a certificate trust bug from the development computer: TLS 1.2 with ECDHE-RSA fails verification using the original R4-only trust store. The server supplies a chain to GTS Root R1 for RSA, and R4 for ECDSA. With both trusted roots, both handshakes verify. RSA returned HTTP 200; ECDSA returned HTTP 429 after the rapid test sequence, confirming TLS completion but server rate limiting. No further requests were sent.

Added R1, DNS checking, differentiated TLS/HTTP errors, serial diagnostics, and longer handshake/connection timeouts. Updated ESP32-S3 build passed. Device-side resolution remains unconfirmed. The application-only binary at 0x10000 is provided to retain NVS settings on the existing partition layout.

## Version 0.1.2 — TLS compatibility diagnostics

User confirmed the feed works on their computer on the same Wi-Fi. Device log shows -80 in ssl_starttls_handshake, with 161,964 bytes free heap and a 94,196-byte largest internal block. No allocation error was reported. This does not establish who resets the connection.

Built upstream Mbed TLS 3.6.2 on the desktop (same version as the ESP32's fork, different platform/configuration). Both default and ALPN HTTP/1.1 handshakes verified. A narrowed ECDHE AES-GCM cipher offer with ALPN also verified to opendata.adsb.fi and pki.goog, negotiating TLS 1.2 / ECDHE-ECDSA-AES128-GCM. This validates server support but does not reproduce the device issue or prove the change will fix it.

The new FeedTLSClient configures the narrowed offer before the deferred handshake using the pinned Arduino core. Certificate and hostname verification remain enabled. Logs now include handshake stage/duration, DNS result and RSSI, plus a one-time control-host handshake after failure. No changes to Wi-Fi credentials, location, partition layout or data parsing.

ESP32-S3 release build for 0.1.2 passed. Application-only and merged images generated and checksummed. Physical result pending.

## Version 0.1.3 — scheduling correction

On-device v0.1.2 logs: feed and control host both failed with -80 at state 11 (MBEDTLS_SSL_CLIENT_FINISHED). Reported handshake times were 104,881 ms and 48,615 ms. These logs support investigating CPU starvation rather than an adsb.fi-only issue; they do not prove the root cause.

Code inspection found the Arduino loop/TLS task at priority 1 and LVGL at priority 2 both pinned to core 1. The canvas redraw ran every 100 ms, with no adjustment for expensive redraws, and LVGL's minimum delay was only 2 ms. A high rendering load can strongly delay the lower-priority TLS operations. Changed the board's ARDUINO_RUNNING_CORE to 0 and explicitly pinned LVGL to 1. The renderer now schedules at max(200 ms, render duration + 50 ms), measured from callback entry. This guarantees a render gap within the timer model. Added task/core and render-duration logs for hardware verification. No partition or settings changes.

ESP32-S3 v0.1.3 release compilation passed (application 1,634,516 bytes). Images regenerated and checksummed. Hardware outcome remains pending.

## Version 0.2.0 — rotation modes and encounter trails

User confirmed v0.1.3 establishes verified TLS and receives live positions repeatedly. Actual rendering was 140 ms, with a 200 ms period on core 1. Core separation remains unchanged.

Added zoom/select modes with visible-aircraft cycling, bold/bright active footer, touch/mechanical click arbitration, and bounded per-aircraft histories allocated in PSRAM. Trails retain encounter start through decimation, simplify straight segments, clip at the circle, and clear on range exit, disappearance or stale positions. Feed snapshots no longer carry histories.

Host tests passed under AddressSanitizer and UndefinedBehaviorSanitizer (LeakSanitizer disabled because this sandbox reports ptrace incompatibility): mode toggles, visible-only wraparound selection, details/back behaviour, both release orders and delayed touch, long-press suppression, range exit/re-entry, stale/removal cleanup, trail capacity/start preservation and segment clipping. Existing JSON tests passed. Native LVGL rendered both modes and verified tap-to-details/back. Visual preview inspected. Physical interaction and performance confirmation pending.

Final v0.2.0 ESP32-S3 release build passed, application 1,625,504 bytes. Footer hit zones checked against rendered text. Merged and application-only images regenerated and checksummed.

## Version 0.2.1 — encoder detent sensitivity

User reported two physical clicks per action. Changed the quadrature accumulator threshold, quotient and remainder from four transitions to a shared two-transition constant. Both rotation modes use the same decoder. ESP32-S3 release build passed. One-action-per-click behaviour awaits device confirmation. Settings and partition layout unchanged.

## Version 0.2.2 — selected path and compass

Selected history is drawn in amber at two pixels/140 alpha, after unselected histories (green, one pixel/45 alpha); aircraft symbols and labels remain above both. Added E/S/W to the existing N marker. Retains 0.2.1 detent adjustment. Native LVGL renders inspected for both modes and existing UI assertions passed. ESP32-S3 release build passed. Physical result pending. README reorganised for sharing; no repository was created or uploaded.

## Version 0.2.3 — EchoScope build tooling

Fresh local Python environment setup passed with pinned PlatformIO 6.2.0 and esptool 4.8.1. `make test` passed model/input and JSON tests. `make firmware` built the renamed ESP32-S3 project and validated the generated application partition at 0x10000 before merging images. Builds reused an existing downloaded PlatformIO toolchain cache through the supported PLATFORMIO_CORE_DIR override. Clean-target testing confirmed tools, library downloads and backups remain. Flash-target dry runs confirmed PORT overrides and correct app/full-image offsets; no device was flashed. GitHub Actions uses a fresh default local cache.
