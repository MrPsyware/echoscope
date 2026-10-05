# Changelog

## 0.16.0

- Original: automatically provide standalone aircraft photos, cached OSM maps, worldwide large airports/custom coordinates, route lookups and weather/cloud forecasts when no capable info server is available. Preserve feature-specific server preference.
- Add progressive JPEG decoding and an eight-thumbnail PSRAM cache, attributed source links, bounded requests and error/rate-limit backoff. Match local map colours and circular edges to server maps.
- Both devices: optional Dr Evil, Nyan Cat, Santa and UFO radar visitors, with callsign-watch opt-in and independent manual HTTP triggers.
- Make logbook encounter tests independent of UTC time of day. No database migration, partition change or mandatory Docker update.
- Publish original ESP32-S3 and Mini ESP32-C3 update/initial-install images and checksums. Full changes, limitations and upgrade instructions: [release notes](docs/releases/v0.16.0.md).

## 0.16.0-dev.6 — unreleased, original ESP32-S3

- Add standalone registration photos through Planespotters.net, including progressive JPEG decoding, bounded PSRAM thumbnail caching and preserved photographer credits/source links. Prefer a capable info server and fall back when it is unavailable.
- Fetch only the visible photo-bearing page, split metadata/image work between live-feed polls, discard obsolete results and back off missing images, failures and rate limits. Keep the full text layout when no photo is ready.
- Update setup and standalone documentation; Mini functionality and its memory use are unchanged.

## 0.16.0-dev.5 — unreleased, both knobs

- Add Nyan Cat with rainbow trail, Santa with sleigh/reindeer, and a hovering UFO alongside the existing rocket. Each has a reserved callsign-watch item and independent manual HTTP trigger.
- Randomly select among enabled visitors, retaining one animation at a time, sleep/page cancellation and the shared manual cooldown. All visitors stay separate from real aircraft data.

## 0.16.0-dev.4 — unreleased, both knobs

- Allow the manual flyby POST independently of the callsign-watch marker. The marker controls random appearances only; screen/page restrictions and the trigger cooldown still apply.

## 0.16.0-dev.3 — unreleased, both knobs

- Add an opt-in novelty radar flyby, enabled by the reserved `dr evil` callsign-watch item, with a manual POST trigger. Keep it separate from real aircraft and never wake the display for it. See [trigger details](docs/EASTER_EGG.md).

## 0.16.0-dev.2 — unreleased

- Match standalone map colours to the info-server palette. Fill pixels outside the map circle with the radar background, removing the contrasting square-corner patches.

## 0.16.0-dev.1 — unreleased, original ESP32-S3

- Add automatic standalone airport, weather/cloud, aircraft-route and map providers. Keep photos, stations and logbook server-only.
- Include 1,153 worldwide large scheduled-service airports and runway geometry, with up to 32 custom coordinates in setup.
- Render maps incrementally in PSRAM and retain on-demand PNG tiles in the existing data partition; preserve fresh tiles for at least seven days.
- Separate standalone settings from server integrations and add host tests for the new data transformations and projection.
- Development only: physical-device validation remains pending; no change to the stable release or Mini behaviour.

## 0.15.0

### Original EchoScope

- Share Mini-style knob navigation: single-click opens a selection, double-click returns to radar or opens Information, hold-and-turn changes radar mode or item page, and a stationary five-second hold opens setup. Retain touch shortcuts.
- Cycle visible aircraft and airport markers in radar selection mode, with an orange airport highlight. Open an airport by clicking the selection or tapping its icon/label; aircraft overlapping an airport retain tap priority.
- Include explicitly requested airports in the information server response even outside the usual nearest-nine list. Preserve airport selection across data refreshes.

### EchoScope Mini

- Add a standalone 240×240 ESP32-C3 target with verified direct adsb.fi HTTPS, streaming JSON decoding, nearest-32 airborne positions and aircraft detail pages. No Docker dependency.
- Add bounded altitude-coloured trails, exact type/registration/callsign watchlists with configurable alert brightness, and 17 built-in UK major airports or up to 32 custom airport coordinates.
- Configure Wi-Fi/location, starting range, brightness, sleep, trails, watchlists and airports in the web interface. Exclude explicitly ground-marked aircraft while retaining airborne low-altitude reports.
- Sample the button independently of rendering; calibrate two encoder transitions per notch. Support single/double-click, hold-and-turn, five-second setup hold and wake gestures.
- Restart after saving settings to recover a clean network/TLS state. Correct active-low backlight control so Sleep is fully dark and brightness increases as configured.
- Rename the target's displayed name and CI artifact to Mini; fix aggregate reset compilation on the GitHub host compiler. Add packaged C3 application/initial-flash images and checksums.

### Information server

- Make the server root a responsive dark logbook dashboard with project information, totals, full-history search, category/date filters, pagination and token-protected photo-cache controls. Keep existing sighting URLs.
- Provide bounded writable `/tmp` for SQLite sorts in the read-only container, fixing `SQLITE_IOERR_GETTEMPPATH` on larger logbooks. Add a production-container regression check.
- Return HTTP 503 and SQLite extended error diagnostics for database failures. Add `make docker-db-check` for read-only integrity/storage diagnostics.

Update the original firmware and information server together for airport shortcuts.
The Mini is standalone and uses USB uploads. Existing device settings and Docker
volumes are retained by the normal update commands.

## 0.14.0

- Keep airport code labels underneath aircraft instead of hiding them near traffic.
- Hide radar controls after 30 seconds; mode clicks and bottom touches reveal and operate them immediately.
- Expand airport approach radar to the main radar size and a 20 km radius, with shared aircraft symbols, trails, sweep and optional airport-centred map.
- Keep map attribution visible in muted green, including with hidden controls.
- Add 20 km map support to the information server; update Docker alongside firmware. Reuse the existing map framebuffer.

## 0.13.1

- Colour highlighted live aircraft trails from each point's recorded altitude, preserving the orange aircraft marker and brighter/thicker selection styling.
- Preserve altitude-colour transitions during straight-line simplification and keep height samples aligned during bounded history thinning.
- Add 48 KiB of PSRAM trail history; no additional network requests or Docker update required.

## 0.13.0

- Apply shared airline-service/size filters before selecting nine nearby airports, preventing closer private airfields from crowding out commercial airports.
- Add airport details/approach pages with consistent item ring, rotate-to-airport, side-arrow page switching and centre-to-radar navigation.
- Show a north-up 10 km approach view with cached actual runway endpoints, nearby fresh aircraft and available trails from the current home feed; label stale/outside/partial coverage.
- Cache the three longest valid open runways per airport from OurAirports, refreshing daily and automatically upgrading airport identifiers in older caches.

## 0.12.0

- Add range-aware airport overlay through the info server, defaulting to airports with scheduled airline service.
- Add Off/Airline/All, airport-size, code-label, colour and brightness controls under Info server features in setup.
- Draw subtle square/runway markers behind trails and aircraft; suppress crowded labels, overlapping markers and text near aircraft.
- Advertise overlay support separately, bound responses to 32 prioritised airports and automatically upgrade existing airport caches with size categories.

## 0.11.0

- Standardise Range, Aircraft, Alt and Type: click to change mode, tap to select, tap again to advance its value. Show Alt and Type values together.
- Keep the selected route/trace page while changing aircraft or log entries; reduce missing-route text to “Unavailable”.
- Group the Information menu and put server-dependent setup options under Info server features.
- Continue sleeping aircraft polling every 30 seconds, with 5-second polling for fresh visible watch matches and optional automatic screen wake/return to sleep. Preserve error backoff and explicit remote sleep.
- Keep interesting-aircraft logbook recording active while the screen is off.

## 0.10.0

- Standardize navigation: clockwise rotation cycles items, outer ring segments highlight the current item, side arrows cycle pages, and centre taps/short presses return to radar. Retain aircraft page slides/swipes.
- Replace the radar INFO button with a stationary two-second touch hold; five-second mechanical setup hold remains separate.
- Move the always-visible filter beside ALT and add watchlist/light/large filters: ALL / WCH / HEL / MIL / LGT / LRG.
- Expand Logbook into details/photo, captured route and static observed-track views with optional OpenStreetMap background; keep recording interesting aircraft only.
- Store bounded encounter tracks and route snapshots, split distinct callsigns/receptions, preserve data gaps and migrate old sightings without inventing missing paths. Add matching web detail/map views.
- Cache attributed photo thumbnails on disk for seven days (up to 256), with a manual clear button at /cache.

## 0.9.0

- Add opt-in authenticated device API and Docker MQTT bridge with Home Assistant discovery, dynamic page/aircraft selectors, brightness, sleep/wake, pickup and aircraft status/list entities.
- Add armed family pickup with fresh-position airport-distance alerts, a 24-hour session limit, persistent daily deduplication and explicit sleep disarming.
- Add persistent bounded spotting history, Today's highlights on the knob and paged web history.
- Add astronomical darkness, Moon illumination, cloud outlook and sunlit station predictions with opt-in wake alerts and live countdown.
- Keep all expensive astronomy/history/broker processing on the server, advertise ready capabilities, and preserve operation without optional services.
- Load Docker integration settings from info-service/.env; continue listening on 0.0.0.0.

## 0.8.1

- Start in aircraft-selection mode, including after saving setup.
- Return to the radar when tapping the centre of either aircraft details or its route page.
- Enlarge the arc touch targets to cover the arc ends and a wider inner band; swipes remain available.
- Reverse INFO menu rotation while keeping page rotation unchanged and selecting the first available item on entry.
- Add previous/next weather side taps with visible arrows and wraparound; centre taps still return to INFO.

## 0.8.0

- Move selected aircraft routes from INFO into a second aircraft-details page: swipe left for route, right for details, with quarter-circle rim markers.
- Add a 380 ms eased slide using two cached PSRAM images; gracefully fall back to an immediate page change if snapshots cannot be allocated.
- Prefetch routes when viewing aircraft details, retain the selected callsign after it leaves coverage, and reset to details when selecting a different aircraft.
- Recognize horizontal swipes without treating them as taps or mechanical clicks; rim markers can also be tapped.
- Simplify weather to next hour, today (including tonight's cloud cover), and the following three days with vector weather icons, local dates/times, temperatures, cloud averages and rain chances.
- Keep weather responses compatible with older text-only firmware and retain existing family-flight tracking.

## 0.7.0

- Add a capability-aware INFO menu for weather, family flight tracking, nearby airports, selected flight routes and space stations.
- Add saved family flight number, optional broadcast callsign override and arrival airport in web setup. Track beyond local radar range; show fresh position, altitude, speed and direct airport distance when known.
- Resolve booking numbers through adsbdb when possible, and handle missing/ambiguous/stale positions without inventing landing or arrival estimates.
- Add local weather and cloud forecasts at three-hour intervals over the next day, including cloud layers, rain chance and day/night labels, via Open-Meteo.
- Cache the OurAirports index on the information server, with optional feature switches and bounded upstream caches.
- Hide unsupported menu options; pause information requests during sleep and suppress stale flight details.

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
