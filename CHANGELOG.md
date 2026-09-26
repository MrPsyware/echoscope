# Changelog

## 0.6.0

- Add a saved startup range in web setup.
- Expand the Docker companion into EchoScope Info Server with backwards-compatible capability discovery.
- Hide absent features and use full flight details until a usable photo has loaded.
- Add a cached, correctly projected faint street-map background with attribution and a capability-dependent toggle.
- Add ISS/Tiangong sky positions and upcoming 10-degree passes using fresh CelesTrak data and server-side SGP4 calculations.
- Rename the source directory to info-service while retaining Compose identity and existing server URLs.

## 0.5.1

- Add saved alert colours for ordinary watchlist, helicopter and military matches.
- Make ring brightness, thickness, effect and pulse/flash period configurable in web setup.
- Default to a subtle 30% peak, 3-pixel, four-second smooth pulse.
- Use category colours for watch markers and deterministic military/helicopter/watch priority for simultaneous alerts.

## 0.5.0

- Add setup-unlocked LAN firmware upload into the inactive OTA slot, with S3 application-header, size and MD5 validation.
- Add `make upload IP=...` and reconnecting `make monitor IP=...` with an 8 KiB application-log buffer.
- Preserve USB commands and reset OTA boot selection during USB app updates.

## 0.4.0

- Add altitude colours and a third knob mode for altitude-band filtering, including unknown altitude.
- Add saved type/registration/callsign watchlists with prefix matching, an A380 alias, and military/rotorcraft options.
- Prioritise watched aircraft at the capacity limit; mark visible matches and pulse a bold green outer ring for fresh matches.
- Add persisted AMOLED brightness and configurable idle sleep (including never) to web setup.

## 0.3.2

- Show the configured Wi-Fi SSID and LAN IP in setup when connected; disable the fallback access point as soon as Wi-Fi connects.
- Start recovery Wi-Fi after 30 seconds disconnected, or when manually opening setup while offline.
- Combine aircraft photos and flight telemetry on one details page, keeping photo attribution and position age while removing bottom control hints.

## 0.3.1

- Fix setup immediately expiring when the loop timestamp predates the portal opening time.
- Require a five-second knob hold for setup.
- Consume pending clicks and the opening touch release so setup stays visible until a new interaction or the normal five-minute timeout.

## 0.3.0

- Add an optional Docker photo service using Planespotters thumbnails, bounded in-memory caching and display-ready RGB565 packets with attribution.
- Save the photo service LAN URL in protected setup, with a connection test and blank-to-disable option.
- Add an on-demand PHOTO view, credit/source-link page, error fallback, standby gating and stale-selection rejection.
- Increase radar radius from 200 to 210 pixels.

## 0.2.9

- Hide the filter after 10 seconds without touch; the invisible target reveals it on the first tap and cycles on subsequent taps.
- Move the north marker to the top of the radar.
- After one hour without interaction, turn the display off and pause rendering/feed requests; consume the first touch, button gesture or rotation to wake and refresh.

## 0.2.8

- Add aircraft class silhouettes, independent military badges and model descriptions.
- Tap the top filter to cycle All / Military / Rotorcraft; filter before the nearest-64 limit and request fresh data.
- Increase radar radius from 182 to 200 pixels and move persistent adsb.fi attribution to details.
- Retain stale-feed warnings, orange selection/trails and existing knob controls.

## 0.2.7

- Sample and debounce the mechanical button independently of slow radar rendering; queue timestamped down/up/hold events for the UI.
- Retain touch overlap suppression and the setup long press; add serial logs for releases and accepted clicks.
- Test short presses, contact bounce, long holds and timer rollover.

## 0.2.6

- Replace response String growth with a bounded PSRAM byte buffer, avoiding Arduino 3.1.1 length truncation above 65,535 bytes.
- Copy exactly the received bytes; avoid the pinned String concat implementation reading one byte beyond unterminated chunks.
- Test large chunked appends, exact capacity, allocation failure and filtered decoding above 64 KiB.

## 0.2.5

- Report the JSON parser stopping offset, nearby printable and hexadecimal bytes, and response tail on decode failures.
- Clarify that connection closure without Content-Length is transport completion, not proof of a complete JSON document.
- Exercise the production filtered decoder and error offsets in host tests.

## 0.2.4

- Log each feed failure with HTTP status, response headers, bounded response prefix, timing, Wi-Fi and memory state, and retry delay.
- Distinguish JSON decoder errors, missing aircraft arrays, interrupted responses, read timeouts, body size limits and allocation failures.
- Log successful response sizes and parsed/retained aircraft counts.

## 0.2.3 — EchoScope

- Rename the application and setup network to EchoScope; retain existing settings storage.
- Add a Makefile with local tool setup, dependency installation, builds, host tests, image packaging, app-only/full flashing, serial monitoring and factory backup.
- Add GitHub Actions build/test workflow and firmware artifacts.
- Document supported hardware, controls, setup, updates, limitations and the planned printable enclosure.

## 0.2.2

- Highlight the selected aircraft trail in amber above other paths.
- Add east/south/west compass labels.

## 0.2.1

- Use two quadrature transitions per detent for one action per physical click.

## 0.2.0

- Toggle rotation between zoom and aircraft selection with a bare knob press.
- Indicate the active mode in the footer; coordinate touch and mechanical clicks.
- Retain faint aircraft paths while they remain visible, storing bounded histories in PSRAM.

## 0.1.3

- Separate network and display tasks onto different CPU cores and limit redraw frequency, resolving the observed TLS stalls on the device.

## 0.1.0–0.1.2

- Initial radar, touch details, Wi-Fi/location setup, live ADS-B data and demo mode.
- Add certificate-chain support and connection diagnostics.
