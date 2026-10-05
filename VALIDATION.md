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

## Version 0.2.4 — feed diagnostics

Existing model/input and JSON host tests passed. Final ESP32-S3 firmware build, app/merged image packaging and checksum verification passed. Reviewed the body reader against the installed HTTPClient API, including its nullable stream pointer after disconnection. Reads now stop explicitly on timeout, size limit, allocation failure or premature connection closure, with bounded printable response samples. The device has not been flashed and intermittent API/network failures have not been reproduced; field logs are still needed to identify their cause.

## Version 0.2.5 — JSON failure location

A fresh HTTP/1.0 response from the configured example endpoint contained 119,361 bytes and 223 aircraft; both Python's decoder and the production filtered ArduinoJson decoder accepted it. This does not reproduce or explain the user's earlier 99,205-byte InvalidInput response. Added tested counting-reader diagnostics: valid filtered data, exact stopping location for an invalid character, truncated input and empty input. Existing host tests and the ESP32-S3 firmware/image packaging build passed. Device failure reproduction and physical validation remain pending.

## Version 0.2.6 — large-response buffer fix

Inspection of the pinned Arduino 3.1.1 WString.cpp found changeBuffer stores the prior length in uint16_t before reallocating, allowing appends beyond 65,535 bytes to overwrite earlier response bytes. Its concat implementation also copies length+1 bytes from receive chunks that were not NUL terminated. Replaced the body with a fixed-capacity PSRAM buffer using exact-length memcpy. Regression tests append and decode a 140 KB JSON document in non-terminated 371-byte allocations, verify every byte and terminator, reject capacity overflow, and handle allocation failure. Existing host tests passed; JSON/buffer tests also passed AddressSanitizer and UndefinedBehaviorSanitizer (leak checking disabled for the sandbox). ESP32-S3 firmware and image packaging passed. The observed field response is consistent with the core defect; confirmation on the physical device is pending.

## Version 0.2.7 — mechanical button sampling

Moved GPIO0 sampling/debounce out of the LVGL timer into a periodic task on core 0, queuing timestamped down/up/hold events for UI-thread processing. The prior nominal 10 ms LVGL polling could pause for a full radar render and miss a short press. Tests cover contact bounce, a press captured during a 140 ms UI pause, single long-hold emission, timer rollover and touch callbacks processed ahead of queued button timestamps. Existing model and JSON tests and the final ESP32-S3 build/image packaging passed. Physical click reliability awaits user confirmation; serial input diagnostics distinguish accepted clicks from touch/long-press suppression.

## Version 0.2.8 — aircraft classes and larger radar

Model tests passed for filter cycling, visible selection and exclusion from hit testing. JSON tests passed for production-filter preservation of description/category/dbFlags, military bitmask combinations, rotorcraft classification/fallback, and military filtering before the nearest-64 cap. Existing input, trail and buffer tests passed. The ESP32-S3 build/package passed (1,643,368 application bytes, 71,868 static RAM bytes). Native LVGL first-frame radar and military-details previews were inspected, and touch/filter/footer navigation assertions passed. Repeated native harness frames had missing draw elements, so preview inspection was limited to fresh first frames; on-device rendering/performance and classification confirmation remain pending. Firmware retains all runtime diagnostics.

## Version 0.2.9 — filter timeout and standby

Host tests passed for 10-second hide, first-tap reveal, subsequent cycling, touch timeout extension, the exact one-hour idle threshold, one-shot sleep/wake, held-input exclusion and timer rollover. Native LVGL first-frame hidden-filter preview was inspected and top-target interaction assertions passed. Existing model/input/JSON tests and the final ESP32-S3 firmware/image packaging passed. Sleep uses the board LCD display-on/off API; the board has no separate backlight. Rendering and new feed requests are gated while asleep, while input sampling remains active. Physical display off/on and an hour-long on-device soak have not been tested; an in-flight HTTP request may complete after sleep begins.

## Version 0.3.0 — optional photo service and LAN configuration

Docker image built with Python 3.12/Pillow 12.1.1. Four service tests passed for image dimensions/RGB565 byte order and attribution, host restrictions, missing-photo caching and invalid-image rejection. `make docker` was exercised with an isolated Compose project and alternate host port; Docker reported HostIp 0.0.0.0. The service health endpoint and a real G-UZHO lookup returned a valid 200×135 image packet (54,392 bytes), photographer Ewan Partridge and the matching Planespotters source link. `make docker-down` removed the verification service; the earlier smoke container was also stopped. Compose configuration validation passed.

Host photo tests cover valid/invalid LAN URLs, dimensions, truncation, bounded credits and provider source links. Model/input/JSON regression tests passed. Native first-frame photo rendering using the real service packet confirmed correct colours and readable attribution; photo/details navigation assertions passed. The larger 210-pixel radar preview was inspected. Physical display/configuration testing is pending. No API credentials or photographs are included in the repository. The setup page still requires the existing 1.5-second physical hold to unlock; the new connection test verifies service identity/protocol. Tests and packaging for the final firmware build are recorded in work/echoscope-030-verified.log.

## Version 0.3.1 — setup timeout and five-second hold

Regression tests cover a loop timestamp one millisecond earlier than portalStarted (previously unsigned subtraction expired setup immediately), the exact five-minute boundary and millis rollover. Button tests verify no setup hold at the old threshold, one hold event at five seconds, and long-press classification on release. Setup entry also clears pending clicks and suppresses a short-touch release originating from the opening gesture. Existing model/input/JSON/photo tests and the ESP32-S3 build/image packaging passed. Physical confirmation of menu persistence is pending.

## Version 0.3.2 — LAN setup and combined photo details

Host model/input, JSON and photo-protocol tests passed. New network-policy tests cover first-boot fallback, the 30-second connection grace period, immediate suppression while connected, subsequent disconnect/recovery and millis rollover. Native LVGL combined-photo and missing-photo previews were inspected, and tap-to-radar assertions passed; connected setup was also previewed. The fallback preview retained the known native harness omission of some unchanged labels, so physical display verification remains necessary. The ESP32-S3 release build and image packaging passed: 1,667,524 application bytes, 72,636 static RAM bytes. No device flashing was performed. Actual AP shutdown/recovery and LAN configuration access require an on-device check. Docker service code and protocol are unchanged.

## Version 0.4.0 — altitude, watchlists and display settings

Host model/input, JSON and photo tests passed. New checks cover the three-mode cycle, altitude boundaries/unknown values, case-insensitive exact/prefix rules, A380-to-A388 aliasing, malformed/oversized watchlists, stale/range/altitude/demo alert suppression, watched-aircraft priority at the 64-entry limit, filtering before that limit, and custom/disabled idle sleep. Model and JSON suites also passed AddressSanitizer/UndefinedBehaviorSanitizer. Native LVGL rendering was used to inspect the new footer, altitude colours and watch ring; the existing native harness still intermittently omits draw elements, so it is not a complete hardware display check.

Final ESP32-S3 build/package passed: 1,674,184 application bytes and 73,492 static RAM bytes. Brightness command framing was checked against the pinned board init sequence (0x51) and SH8601-compatible QSPI driver; boot, save and wake apply the saved value, serialized with LVGL where active. Physical brightness, mode interaction, ring animation and persistence across reboot still need checking on the device. No flash was performed. Satellite tracking and wireless updates are not included; the Docker service is unchanged.

## Version 0.5.0 — wireless upload and network logs

The generated partition table was inspected: app0 at 0x10000 and app1 at 0x650000, each 0x640000 bytes, with existing OTA metadata at 0xe000. No partition change was made. The final release build/package passed: 1,684,228 application bytes and 82,004 static RAM bytes, increases of 10,044 and 8,512 bytes respectively from 0.4.0. Runtime upload buffers are additional temporary allocations.

All existing host tests passed. Three Python tests cover IP normalization/rejection, multipart upload metadata and MD5, incompatible target/capacity/bootloader-image rejection, monitor cursor reset on reboot, overwritten-data handling and reconnection. USB and IP Makefile routes were dry-run checked. Firmware upload code accumulates the first 36 bytes across chunks, rejects non-S3/non-application headers, enforces the declared size and MD5, aborts incomplete writes, and selects/reboots only after successful Update.end(). Failed-runtime firmware is not automatically rolled back. Actual over-Wi-Fi flashing, reboot and long-running log monitoring have not been hardware-tested; one USB installation is required first. Network logs cover application/TLS diagnostics, not SDK/ROM/panic output.

## Version 0.5.1 — configurable alert appearance

Model tests passed for default pulse brightness/width/period, smooth fade samples, cycle wrap, steady/flash/off/zero-brightness behavior, millis rollover bounds, strict hex-colour parsing and simultaneous-category priority with stale-category removal. Model tests also passed AddressSanitizer/UndefinedBehaviorSanitizer. Existing model/input/JSON/photo/network-client tests passed; the ESP32-S3 firmware and packaged images built successfully. A native LVGL preview confirmed the thin, dim ring at the pulse peak and category-coloured markers; temporal smoothness and actual perceived brightness require physical verification. No flashing was performed. Web values are validated before applying/persisting, restored with bounds on boot, and copied to the UI while holding its lock.

## Version 0.6.0 — startup range and information server

Final firmware build/package and all existing host suites passed (including startup-range reset/preservation and bounded map-packet validation): 1,700,084 application bytes, 82,612 static RAM bytes. Model and image-protocol suites also passed AddressSanitizer/UndefinedBehaviorSanitizer. The map consumes 352,800 bytes of persistent PSRAM plus a similarly sized temporary response buffer; these are not included in static RAM. No hardware flash or performance measurement was performed.

Nine information-server tests passed for photos, capability disabling, inverse-map projection (including dateline/southern hemisphere), map packet/dimensions/cache reuse, query validation, known-epoch station propagation/pass bounds, and stale-orbit exclusion. Live CelesTrak stations JSON produced fresh ISS/Tiangong positions and future 10-degree passes. A live OpenStreetMap London/25 km request produced the expected 352,808-byte packet. Native LVGL previews were inspected for map alignment/attribution, the station sky view, and full text details with no available photo; station rotation/return assertions passed. Map attribution uses ASCII 'Copyright' because the current font lacks the copyright glyph. Physical smoothness, map readability and interaction require confirmation on the knob.

The Docker image built and ran with read-only root, unprivileged user and 256 MiB limit. A combined smoke test advertised all three capabilities, returned station JSON and rendered a map from the persisted tile cache; measured memory after requests was 59.17 MiB. The actual make docker command was exercised under an isolated Compose project, with all capabilities disabled and INFO_PORT=18088. Health correctly advertised all false; Docker reported HostIp 0.0.0.0. The new cache volume was writable by the service user. Test containers and their temporary Compose volume were removed. Existing Compose identity and legacy photo endpoint/health identity remain for upgrades. Docker/runtime integration was checked in Python 3.12; local Python 3.14/NumPy 2.5 emits a Skyfield deprecation warning but tests pass.

## Version 0.7.0 — family flights and observing weather

The host model/input, JSON/photo protocol and network-device tests passed, together with 16 Python information-server tests. New cases cover booking-number resolution and callsign overrides, stale/untimed feed rejection, duplicate/wrong/stale aircraft, unavailable routes, arrival-airport mismatch, missing cloud values, bounded weather payloads, negative-cache backoff, airport-index filtering/persistence, dateline distances and disabled-capability endpoints. The Docker image builds with the new module explicitly included in its allowlisted build context.

Live provider checks returned Open-Meteo weather/cloud layers, adsbdb booking/route information and the OurAirports airport index (48,028 retained open small/medium/large airports in this snapshot). A live adsb.fi callsign lookup also returned position/altitude/speed data. Tests do not establish reliable airline schedule information; the UI explicitly avoids inferred ETA/landing claims and distinguishes route-database information from actual operations.

The final Docker image ran as its unprivileged user with read-only root and a 256 MiB limit. All six capabilities were advertised with fresh cached orbital/airport data. Weather, route, nearby-airport, family-flight and station endpoints returned HTTP 200; their payloads ranged from 242 to 1,776 bytes, below the knob's 8 KiB bound. Combined-service memory after these requests was approximately 82.1 MiB. Access logs were checked to omit query strings containing coordinates or flight numbers. The temporary test container was removed. Existing 0.0.0.0 Compose binding and upgrade identity remain unchanged.

Native LVGL renders of the INFO menu, cloud forecast and family-flight pages were visually inspected at 466x466. Assertions exercised rotary menu navigation, page wraparound, press/tap back, and skipping disabled options. Long text reduces font size to remain within its allotted row. Routes retain the callsign selected when opened, and reopening a page requests a refresh. Family pages hide after 60 seconds without fresh data; weather/other pages expire after 30 minutes. These are host/build checks, not confirmation on the physical knob. Actual hardware interaction, LAN discovery, saved settings and wireless upload still need the user's device test.

Final firmware/package build passed with 1,708,112 application bytes and 87,012 static RAM bytes: increases of 8,028 and 4,400 bytes respectively from 0.6.0. Each OTA slot remains 6.25 MiB; no partition changes are needed. JSON and networking also use temporary runtime allocations, so static RAM totals are not total heap usage.

## Version 0.8.0 — aircraft route swipes and compact forecasts

Host input/model, JSON/photo protocol and network-device tests passed. New gesture assertions cover left/right swipes, tap-sized movement, short drags and vertical movement. All 18 information-server tests passed, including next-hour selection across midnight, the matching next-day subtitle, icon mapping, unknown values, incomplete forecasts, the following three daily dates, bounded payload size and tonight's cloud calculation excluding the previous morning.

A native LVGL harness exercised forward/reverse transitions, animation completion and image-object cleanup, ignoring a second swipe during animation, disabled route capability, low-memory instant-page fallback, changing the selected callsign and removal of route entries from INFO. The preview LVGL objects were rebuilt from the project's current 192 C source files to avoid inconsistencies from older local object files. Current-source 466x466 renders of aircraft details, the route rim marker, next-hour sun/storm icons and the three-day cards were inspected. Two 466x466 RGB565 PSRAM snapshots use 868,624 bytes; labels are rendered before the 380 ms animation rather than on every animation tick. Real screen frame rate, touch gesture reliability and perceived smoothness still require physical-device testing.

The Docker image built and ran with read-only root, an unprivileged user and its 256 MiB limit. Live `/health`, `/v1/weather` and `/v1/route` requests returned HTTP 200; the new weather payload was 1,663 bytes and had exactly three views with three daily cards in the final view. Text fallback lines remain present for older firmware. The test container was removed. Existing Docker binding, server URL configuration and firmware partitions are unchanged. No hardware upload was performed.

Final firmware/package build passed: 1,724,292 application bytes and 93,164 static RAM bytes. Compared with 0.7.0 this adds 16,180 application bytes and 6,152 static RAM bytes, plus the optional PSRAM snapshots noted above. The application occupies 26.3% of its unchanged OTA slot.

## Version 0.8.1 — control refinements

Host tests and the firmware/package build passed. Model assertions cover aircraft-selection defaults on construction and reset, saved startup range, and the existing mode cycle. A native LVGL navigation harness verified centre taps returning directly to radar from both aircraft pages, enlarged arc targets at the inner band and curved endpoint, animated page switching, reversed INFO menu steps, skipping unavailable items and selecting the first visible item on menu entry. Weather side taps were checked in both directions, including wraparound, with centre-to-INFO behaviour and unrelated information pages preserved. A 466x466 weather preview with the new side arrows and touch hint was inspected.

Final build: 1,724,440 application bytes and 93,164 static RAM bytes. This is a firmware-only update; the information server is unchanged. Physical knob direction and touch feel still require device confirmation. No hardware upload was performed.

## Version 0.9.0 — connected observing

- Firmware model/JSON/photo/network host checks and ESP32-S3 release build passed.
  Application 1,739,348 bytes (26.5% of the 6,553,600-byte OTA slot), static RAM
  93,348 bytes (28.5%). Runtime allocations are additional.
- 28 Python tests passed: existing photo/map/orbit/weather/flight tests plus
  command validation, discovery/state, durable deduplication, stale pickup rejection,
  bounded sighting retention, HTML escaping, station visibility conditions and
  cloud forecast validation/cache behavior.
- Isolated Mosquitto 2 broker + authenticated simulated-knob HTTP endpoint:
  automatic discovery, dynamic status, brightness, page, aircraft, sleep/wake,
  retained-command rejection, Home Assistant birth rediscovery and device-offline
  availability passed. This is not a test against the physical ESP32 or user's HA.
- Docker built and ran with read-only root, dropped capabilities and 256 MiB limit.
  Live JPL ephemeris, CelesTrak station prediction and Open-Meteo cloud requests
  returned a bounded valid stargazing packet with darkness, Moon and an ISS pass.
  Container used about 85 MiB after the request; no OOM recorded. This is an observed
  smoke-test value, not a maximum for every configuration.
- Native LVGL build rendered and visually checked the six-entry INFO menu,
  stargazing text layout and live countdown alert. Menu wrap, page side taps and
  alert dismissal assertions passed.
- Firmware, Docker image and setup documentation prepared together. No flash or
  change to the user's MQTT/Home Assistant installation was performed. Remaining
  on-device check: token-enabled control responses, overnight alert timing, actual
  feed/HTTP latency and sleep/wake interaction on the knob.

## Version 0.10.0 — unified navigation and illustrated logbook

- Firmware host model/input/JSON/photo/network checks and ESP32-S3 build passed.
  Application: 1,764,188 bytes (26.9% of OTA slot); static RAM: 96,900 bytes (29.6%).
- 36 Python service tests passed. Added old-history migration, bounded track
  sampling, reception gaps versus stationary updates, transient missing callsigns,
  late registration/type metadata, route snapshots, bounded packets, escaped web
  detail strings, photo disk-cache expiry/corruption/clearing and cache-button token.
- Native LVGL assertions exercised grouped/non-contiguous item navigation, page
  wrap, detail-to-radar, menu selection, logbook arrows and filter controls. Native
  renderings of the actual firmware UI were inspected: radar footer, segmented
  aircraft/menu rings, sighting text/photo, route, grid track and OpenStreetMap track.
  Renderings used labelled fixture flights/paths, not the user's live aircraft.
- Docker build passed. Isolated read-only-root/256 MiB container returned log detail
  JSON, attributed real thumbnail, web detail HTML and 352,808-byte historical map
  packet. The clear-cache form returned HTTP 200 and removed thumbnails while the
  fixture SQLite history and map tile cache remained present.
- Two-second touch-hold threshold, one-shot firing, blocked hold and millis rollover
  are covered in host tests. Physical touch duration/rotation and on-device frame
  timing still need user confirmation. No firmware was flashed and no user Docker
  deployment/history was modified; migration ran against test databases only.

## Version 0.11.0 — controls and sleep monitoring

- Host model, JSON, photo and network-tool tests passed. New model checks cover all four modes, tap-to-select/advance, sleeping 30/5-second cadence, optional automatic wake, departure, stale timeout, user takeover, explicit remote sleep and timer rollover.
- All 37 info-service tests passed, including fresh sleeping logbook capture and concise unavailable routes; Docker image built successfully.
- Native LVGL rendering inspected for the four-control radar footer, reordered six-item Information menu and unavailable route page. Interaction assertions passed for route-page retention (including missing callsign), logbook trace retention, menu ordering and grouped subpages.
- Release firmware built and packaged: application 1,766,048 bytes; static RAM 96,916 bytes. No new framebuffer allocations.
- Hardware was not flashed. Verify physical footer taps, idle polling and watch-triggered wake/return on the knob after updating. Error retry backoff remains in force; the sleep intervals are delays between requests, not guaranteed wall-clock sample periods.

## Version 0.12.0 — airport overlay

- Host model/JSON/photo/network-tool tests passed; 41 info-service tests passed. Added coverage for scheduled-service and size filters, radius, date-line proximity, bounded/prioritised results, invalid queries, capability gating and legacy airport-cache migration.
- Docker image built; an isolated read-only container returned the overlay capability and a filtered airport packet over HTTP, including an empty result outside the selected range. Test container stopped afterwards.
- Native LVGL render inspected with sample London-area airport positions and a synthetic 32-airport crowding case. Assertions cover label on/off, zero brightness, unavailable capability and range mismatch. Markers are rendered before sweep, trails and aircraft. Physical display brightness/legibility still needs checking on the knob.
- ESP32-S3 release build passed using two compiler jobs after the initial full parallel build exceeded local memory. Application: 1,773,192 bytes; static RAM: 97,716 bytes. Firmware packaged with checksums. No new framebuffer allocation and no device flashing performed.

## Version 0.13.0 — airport list and approach pages

- All 45 info-service tests passed. Added tests for filtering before the nine-airport limit, shared size filtering, empty results, runway endpoint validity/closed-runway rejection, cache reuse and bounded page payloads.
- Real OurAirports data at test coordinates 51.5, 0 returned LCY, LHR, LGW, STN, SEN, LTN, SOU, BOH and NWI with Airline/Any filters. Gatwick supplied both 08R/26L and 08L/26R endpoint pairs. Packet size: 3,040 bytes.
- Docker build passed; a read-only test container using that cache served the filtered airport list and Gatwick runway metadata over HTTP. All versus Airline queries returned different lists. Test container stopped afterwards.
- Native LVGL render checked for airport details, approach radar and stale data. Assertions passed for airport rotation preserving the approach/details page, both side arrows and centre return. Aircraft originate from the home feed; outside/partial coverage is labelled and demo/stale aircraft are suppressed.
- Final ESP32-S3 build and firmware packaging passed: application 1,777,688 bytes; static RAM 98,828 bytes. No extra framebuffer or aircraft polling added. Device not flashed; physical touch/legibility remains to be checked after updating.

## Version 0.13.1 — historical altitude trail colours

- Host model, JSON, photo and network-tool tests passed. New checks cover altitude-band transitions on straight tracks, unknown altitude, repeated same-position frames, aligned position/height history thinning and colour lookup.
- Native LVGL preview inspected using a sample climb from 1,000 to 40,000 ft. The selected trail progresses through green/cyan/blue/purple while the aircraft/label remain orange. Recorded start/end altitude assertions passed.
- ESP32-S3 build and packaging passed: application 1,777,868 bytes; static RAM 98,828 bytes. Per-aircraft trail heights add 49,152 bytes (48 KiB) to the existing PSRAM history allocation. No device flashing performed.

## Version 0.14.0 — unobstructed radar and 20 km airport maps

- Host model, JSON, photo and network-tool tests passed; all 46 information-service tests passed. Added timer expiry, visible/hidden rotation and millis rollover checks, plus 20 km map projection/cache acceptance.
- Native LVGL assertions passed for unchanged airport labels with nearby aircraft, label enable/disable, hidden footer touch acting immediately, airport page retention, side arrows/centre return and rejection of maps for a different airport. Inspected the expanded radar with a synthetic circular map fixture; compass labels are clear of navigation arrows.
- ESP32-S3 build and packaging passed: application 1,779,080 bytes; static RAM 99,140 bytes. Airport maps reuse the existing framebuffer and are requested only for the visible airport. No additional aircraft polling was added.
- Both firmware and server must update for 20 km map support. The view continues using the home aircraft feed and labels partial/outside coverage; it is not an independent receiver at the airport.
- No device was flashed. Physical brightness, footer touch and the airport-centred live map still need checking on the knob.

## Server database diagnostics

- All 49 server tests passed. New checks cover valid/missing/invalid database files without modifying them, extended error reporting and HTTP 503 across all six logbook routes, including semaphore release.
- This addresses error handling and diagnosis, not a confirmed storage root cause. The user's Docker volume has not been accessed or repaired.
- Packaging follow-up: added the diagnostic script to `.dockerignore`'s allowlist after the first server build exposed its omission. Full Docker build now passes; the built image successfully ran `db_check.check` against an isolated temporary database as uid 65534 with a read-only root and no network/existing volumes.

- Reproduced the logbook's disk I/O failure in the old read-only container with 1,000 synthetic encounters and 8 KiB tracks. The same query rendered all 500 page entries under the updated production Compose settings, using a separate temporary project/volume. Added this check to CI and removed the local test volume afterwards. Existing database data is unchanged; only ephemeral `/tmp` storage is added.

## Logbook dashboard

- All 53 server tests passed, including full-history search, literal wildcard handling, SQL metacharacters, escaping, combined category/date filters, pagination and invalid input. Root/legacy routes and token-protected cache POST/redirect tested.
- Production Docker build and isolated Compose logbook smoke test passed with 1,000 synthetic encounters; the temporary volume was removed.
- Inspected headless Chromium renders at 1280 px and 390 px using labelled sample aircraft. Mobile navigation stacks and the table scrolls within its container. No live user database was modified.

## ESP32-C3 Mini prototype

- Hardware identification confirmed ESP32-C3 revision 0.4 / 4 MB flash. Original full flash backed up and SHA-256 recorded locally. Hardware test uploaded and user confirmed button gestures; encoder calibrated to two transitions per notch with reversed sign.
- Mini host tests passed for input timing/bounce/wrap, five-second setup threshold, free versus held rotation, incremental framing, escaped/nested strings, all truncation points, invalid/duplicate arrays, oversized objects, nearest-32 selection and geographic date-line handling. A public live adsb.fi response at fixture coordinates decoded successfully.
- Existing ESP32-S3 host regression tests passed. Mini radar, details and setup layouts rendered natively and visually inspected at 240×240.
- Mini build: 1,255,584 bytes application / 1,966,080-byte slot; 53,220 bytes static RAM. Uses a 7,680-byte DMA strip buffer, no PSRAM. Separate four-megabyte partition layout includes two application slots, NVS and a core-dump partition; OTA upload is not implemented.
- Uploaded to the C3 with flash hashes verified. Initial setup AP ran with about 145,000 bytes free heap (roughly 142 KiB), largest block 114,676 bytes. End-to-end feed/TLS memory testing on the knob awaits the user's Wi-Fi/location setup. No saved credentials are checked in or logged. The main ESP32-S3 firmware is unchanged.

## Mini ground filter and live resource check

- Exclude explicit `alt_baro: "ground"` before inserting a candidate, including when a geometric altitude is present. Regression test retains numeric zero, negative and low positive airborne altitudes. Mini parser tests passed; build and C3 flash hashes verified.
- Seven live adsb.fi HTTP 200 requests succeeded with verified TLS 1.2, at 25 km with two aircraft (about 1.5 KiB responses). Post-request free heap remained 154,288 bytes; minimum observed heap 79,912 bytes; largest idle internal block 114,676 bytes. Radar render time 59–60 ms at a 200 ms target period. This is a short, light-traffic sample, not a full-capacity endurance benchmark.
- Application 1,255,684 / 1,966,080 bytes (63.9% of its slot); 710,396 bytes remain in that slot. Static RAM remains 53,220 bytes. No new large buffer allocation. Ground reports were absent in this live sample; ground exclusion is covered by the targeted parser regression.

## Original knob navigation and airport touch links — 4 October 2026

- ESP32-S3 release build and firmware packaging passed. App image: 1,781,456
  bytes; linker flash usage 1,781,092 / 6,553,600; static RAM 100,052 bytes.
- Host model/input, JSON, photo/protocol and network-device checks passed.
  Added double-click suppression, hold-turn cancellation and airport icon/label
  hit-area checks. All 54 information-server tests passed, including explicitly
  selecting an airport outside the nearest nine and invalid/missing targets.
- Wireless application upload to the original at 192.168.2.151 succeeded after
  the user unlocked Setup. Device verified the image and restarted; logs show
  live positions, a successful feed and airport markers loading after reboot.
- Physical single/double-click, hold-and-turn and airport touch confirmation is
  still pending user feedback. Mini firmware was not changed or reflashed.
- Airport targeting requires the accompanying information-server update; its
  source is updated here, but the user's Docker host was not redeployed.

## Radar airport selection and Mini recovery/power fixes — 4 October 2026

- Original radar selection now wraps across visible aircraft and drawn airport
  markers, using stable airport codes across feed refreshes. Host tests cover
  aircraft/airport boundaries, reverse wrap, missing selections and airport-only
  lists. The existing model, JSON, protocol and network-tool checks also pass.
- Both targets build successfully. Original app image: 1,782,384 bytes; Mini
  linker flash usage: 1,280,736 bytes, static RAM 68,044 bytes.
- Original wireless upload succeeded after physical setup unlock; reboot logs
  confirm a successful live feed and airport overlay loading.
- Mini uploaded over USB (hardware serial 34:CD:B0:CE:55:88, now /dev/ttyACM1).
  Submitted all existing web settings unchanged: response confirmed restart,
  boot/form-token change verified restart, and every saved field matched after
  reconnection. Serial logs then confirmed three verified-TLS HTTP 200 feeds.
- Mini backlight polarity is inverted relative to the pinned library's board
  profile. The manufacturer schematic shows GPIO8 driving the CJ3407 P-channel
  gate (active low). Brightness mapping now compensates in one helper used for
  startup, configuration, sleep and wake; endpoint/clamping tests pass.
- User confirmed the Mini backlight goes fully dark in Sleep and wakes normally.

## Release 0.15.0 packaging — 4 October 2026

- Updated the original, Mini and server version strings, README/control guides,
  changelog and release notes for the shared 0.15.0 release.
- Original host checks, Mini input/feed/feature tests and all 54 server tests pass.
  Both firmware targets build and package successfully.
- Added `make mini-firmware` and CI packaging under `dist/mini/`, with separate
  C3 application/merged filenames and `MINI-SHA256SUMS`.
- Verified S3/C3 chip IDs in both applications and merged bootloaders, compared
  each merged application's bytes at 0x10000 against its application image, and
  verified every checksum. Local app images: original 1,782,384 bytes;
  Mini 1,327,280 bytes. GitHub release assets are taken from the matching green
  Actions run, rather than older local build output.
- No additional device flashing is part of release preparation. Prior entries
  record the hardware feature, save/reconnect and sleep/wake checks.

## 0.16.0-dev.1 — standalone original development branch

On `dev/standalone-pro`, host model/input, JSON, photo, network-client and new
standalone tests pass. New checks cover airport coordinates/overrides/pinned
selection, forecast missing values and page structure, routes and map projection
including dateline wrapping. The original ESP32-S3 firmware builds and packages:
2,032,948 bytes linker flash use (31.0% of OTA slot), 101,852 bytes static RAM
(31.1%). Live Open-Meteo and adsbdb responses passed the production C++ normalization
functions on the host; both endpoints verified with the firmware's root set.
The OSM tile host requires GlobalSign Root R3, verified separately and included.
No unviewed tile batch was downloaded for validation.

No device was flashed. First-boot LittleFS creation, PNG decoding on ESP32, map
cache reuse, all-range/airport map alignment and frame/input/feed timing remain
physical-device checks before release. See `docs/STANDALONE.md`. Main and the
published 0.15.0 release are unchanged.

The first branch CI run exposed two pre-existing wall-clock-dependent logbook
tests when simulated flights crossed midnight. Their fixtures now start at a
fixed UTC noon, with a separate midnight-boundary assertion. All 55 info-service
tests pass locally; no server runtime code changed.

## 0.16.0-dev.2 — standalone map palette and circle background

Matched the local RGB565 palette to the info-server renderer and initialized
pixels outside the circular viewport to the canvas background (0x030D10), replacing
black corner regions. Added reference palette checks for land, water, grayscale
and primary colours. All host tests passed; original firmware build and packaging
passed (2,033,164 bytes linker flash; 101,852 bytes static RAM). Physical visual
confirmation is pending upload; the original currently requires Setup unlock.

## 0.16.0-dev.3 — opt-in novelty flyby, both devices

Original host suite and Mini feature tests pass, including marker parsing and
exclusion from real callsign watches. Shared scheduler tests cover disabled state,
manual requests, 20-second lifetime, random interval bounds, leaving radar, sleeping,
millis wrap and circular stroke clipping. A preview rendered from the actual
vector-stroke output was inspected at both display scales.

Both firmware builds and app/merged packaging passed. Original: 2,035,896 bytes
linker flash, 101,876 bytes static RAM; Mini: 1,284,316 bytes linker flash, 68,076
bytes static RAM. The effect uses no heap/image buffers or additional network data.
Neither device has been flashed with this version. Physical visuals, long-duration
appearances and POST triggering remain to be checked on both devices.

## 0.16.0-dev.4 — independent manual flyby

Both firmware builds and packaging pass. Original and Mini feature tests pass;
shared tests now confirm a manual flyby renders and expires without any watchlist
marker, does not arm random appearances, and is cancelled by leaving radar/sleep.
Removing the marker still cancels an automatic appearance. HTTP retains the custom
header, awake-radar requirement and 30-second manual cooldown on both devices.
Device upload/HTTP confirmation remains pending physical Setup unlock.
