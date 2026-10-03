# Home Assistant, pickup and observing

EchoScope uses the Info Server as a bridge. The knob keeps its small local
HTTP control API; Docker does MQTT, astronomy, background flight lookups and
SQLite history. One Info Server supports one knob. You do not need MQTT to use
the spotting log or observing/pickup alerts.

## Setup

1. Update the knob firmware and this Docker service to 0.11.0 or newer.
2. Hold the knob for five seconds and open its setup page. Enable **authenticated
   LAN integration**, save, and copy the **Device API token**. Its IP should have
   a DHCP reservation. Keep the existing Info Server URL configured on the knob.
3. On the Docker server, copy `info-service/env.example` to `info-service/.env`.
   Set `ECHOSCOPE_URL` to the knob URL (for example `http://192.168.2.151`) and
   `ECHOSCOPE_TOKEN` to the copied token. Add your existing MQTT broker's address
   and credentials, or leave `MQTT_HOST` blank if you do not want MQTT.
4. Run `make docker`. It explicitly loads `info-service/.env` when present and
   continues to publish port 8086 on **0.0.0.0**. Use `make docker-logs` for errors.
5. In Home Assistant, configure the MQTT integration against the same broker.
   **EchoScope** appears automatically through discovery. No manual YAML is needed.

For a TLS broker use `MQTT_TLS=1` and `MQTT_PORT=8883`. Certificate/hostname
verification uses the container's system trust store. No anonymous broker is
created by this service. The device token and HTTP service are intended for your
trusted LAN; do not publish their ports on the Internet. `.env` is ignored by Git.
Changing/disabling LAN integration takes effect immediately after saving setup.
The token is persistent, independent of the temporary firmware-upload token.

## Home Assistant controls and topics

Entities: page selector, aircraft selector, screen on/off switch, pickup switch,
brightness number (5–100%), aircraft count with the complete received aircraft
list as attributes, selected-aircraft sensor with status attributes, and last alert.
Unavailable optional pages disappear from the selector. Aircraft options contain
ICAO hex and callsign/registration; the hex distinguishes similar or missing callsigns.
The list contains up to 64 fresh aircraft from the knob's received feed, including
positions outside the current display range. `visible` indicates current filters/range.
Selecting an aircraft opens its details, even when it is outside the displayed range.
A vanished/stale aircraft command fails instead of selecting another aircraft.

Topic prefix: `echoscope/<lowercase-MAC-without-colons>`.

| Topic suffix | Payload |
| --- | --- |
| `set/page` | `radar`, `aircraft`, `route`, `information`, `stations`, `weather`, `family`, `airports`, `stargazing`, `logbook` (available pages only; `highlights` remains an alias) |
| `set/aircraft` | ICAO hex, e.g. `40756d`, or the exact dropdown option |
| `set/brightness` | Integer `5`–`100` |
| `set/screen` | `ON` or `OFF` |
| `set/pickup` | `ON` or `OFF` |
| `aircraft` | JSON object containing `aircraft` array |
| `selected_status` | Selected aircraft JSON, or explicit no-fresh-position status |
| `family` | Current pickup tracking status, while armed |
| `observing` | Moon illumination and visible passes, while station alerts enabled |
| `event` | Non-retained JSON `{kind,message,time}` for `pickup` or `station` |
| `command_result` | JSON `{ok,command}` acknowledgement |
| `availability` | `online` / `offline` |

Device state is polled every five seconds; existing device HTTP/feed work can add
latency. Availability follows successful polling and the MQTT last will. Discovery
is retained and republished after Home Assistant/broker reconnect. Commands must
**not** be retained; retained commands and commands queued for over 15 seconds are
ignored. Queue sizes are bounded during broker outages. State topics are refreshed,
not retained as a stale aircraft history. Brightness is runtime-only; save your
preferred boot brightness in web setup. Explicit sleep disarms pickup. Aircraft polling continues every 30 seconds, or
every 5 seconds while a fresh watched aircraft is visible. Logbook recording
continues with fresh data while the screen is off. HTTP controls stay available.
Explicit screen-off suppresses automatic watch wake until the match has left. Controls
return busy during setup, firmware upload or page animation rather than interrupting them.

## Family pickup

Configure flight number, optional actual callsign, arrival airport IATA/ICAO and
**pickup alert distance** (5–1000 km) in knob setup. Arm via its checkbox or the
Home Assistant pickup switch. This opens Family Flight and prevents idle sleep.
You can browse other pages while it stays armed. An armed session expires after
24 hours; reboot or explicit screen sleep also disarms it. Arming is not saved to flash.

Docker checks every 30 seconds. It alerts once for that flight/callsign/airport/
threshold per UTC day, when a single matching position no older than 60 seconds
is within the configured direct distance. Arming when already inside also alerts.
Missing airport coordinates, duplicate callsigns, stale data or no position never
produce an arrival alert. No ETA, delay or landed state is inferred. Confirm the
flight/date and pickup time with the airline. The screen shows a 30-second alert;
Home Assistant also receives its event. Provider failures retry without disabling
station alerts. Daily deduplication survives container restarts in `/data`.

## Stargazing and “look up” alerts

The INFO menu gains **Stargazing tonight** when the cached JPL ephemeris is ready.
It contains astronomical darkness windows for the next 24 hours, Moon illumination,
dark-hour cloud average, clearest forecast hour and upcoming visible space-station
passes. Times are UTC. An interval extending past the prediction horizon is labelled
as continuing, not as ending at that time. Side taps or rotation change pages.

Station predictions use fresh CelesTrak orbits, observer Sun altitude below −6°,
a sunlit station and elevation at least 10°. They are sampled every 15 seconds;
maximum elevation refers to the visible part of the pass. These are geometric
opportunities, not a guarantee of visibility. Cloud cover comes from the nearest
hourly Open-Meteo forecast; if it fails, the view explicitly says unavailable.
Astronomical darkness uses Sun altitude below −18°. Moon illumination alone does
not describe sky brightness or whether the Moon is above your horizon.

Enable **Alert / wake for visible station passes** in knob setup to opt in.
The server alerts up to two minutes ahead, showing a live countdown, viewing
bearing, peak elevation and cloud cover. An alert can wake the screen even during
idle sleep. It stays until 30 seconds after the predicted start, or tap/press to
dismiss. Each pass is deduplicated across restarts. No buzzer or sound is required.
Set `ENABLE_STARGAZING=0` to disable the feature. First startup downloads a roughly
17 MB DE421 ephemeris into the persistent cache; it is valid for 1900–2053. Existing
Space Stations remains the geometric overhead view and can work independently.

## Spotting log

With device integration connected, Information gains **Logbook**. Rotate through
the latest eight encounters; the ring highlights the selected entry. Side arrows
cycle sighting details/photo, captured database route, and observed path on a static
radar map. Photos never reserve a blank frame. The map uses a subdued OpenStreetMap
background when available and otherwise keeps the grid and track. It is north-up,
centred on the home location when the encounter was recorded, and sized to fit the
saved track (up to 100 km). Start is green; last recorded position is orange.

`/sightings` on the Info Server provides paged full history. Open an entry for the
same photo, route and an SVG track map. `/cache` has a **Clear photo cache** button:
it clears disk thumbnails and the memory cache, preserving maps, orbits and history.
Photos are cached for seven days, up to 256 files (about 15 MB), with photographer
credit and the original source link kept with every thumbnail.

Only interesting aircraft (watchlist, military and helicopters) are recorded from
the knob's received feed while awake. A different non-empty callsign, home location,
UTC day or a gap of at least 15 minutes starts a new encounter. Missing transient
callsigns do not split the flight. Tracks keep at most 192 sampled points, preserving
the beginning by decimating long paths. Reception gaps over 60 seconds are not joined;
a dot with no line is a single observation. These are observed paths, not filed
flight plans. The route is an adsbdb snapshot with its lookup time, normally captured
in the background or when an older entry is first opened; it may differ from the
actual flight. A callsign can be reused. No unobserved path is reconstructed.

Data stays in `/data/sightings.sqlite3` in the existing named volume, for 30 days
and at most 10,000 encounters. Existing daily records migrate once, preserving their
sighting details. They have no recoverable past path; the track page says so.

New endpoints: `/v1/logbook/ID` (bounded route/track JSON), `/v1/logmap/ID` (same
420×420 RGB565 map protocol), `/sighting/ID` (web detail), `/sighting-map/ID` (PNG),
`/image/REGISTRATION` (thumbnail PNG). Historical maps render on demand and return
202 while being built. The old `/v1/highlights` listing remains compatible and now
includes entry IDs/registrations. Missing capabilities/data never invent a route,
position or photo. The firmware's photo source link also works for logbook photos.

## Device HTTP API

Disabled by default. Every request needs `X-EchoScope-Token: <token>`.
`GET /api/state` returns current pages, controls, fresh aircraft, selected hex,
location and family settings. It never returns Wi-Fi passwords or tokens.
`POST /api/control` takes one JSON command with Content-Type `application/json`:

```json
{"page":"weather"}
```

Other commands: `{"aircraft":"40756d"}`, `{"brightness":35}`,
`{"screen":false}`, `{"pickup":true}`. The bridge's alert command is
`{"notify":{"message":"Look up","countdown":0}}`; a nonzero countdown is a UTC
Unix second within the next three minutes. Messages are limited to 120 characters.
Responses: 200 accepted, 400 invalid, 403 disabled/unauthorized, 404 unavailable
page/aircraft, 409 busy or missing pickup configuration. POSTs are bounded to 512
bytes at command validation. OTA upload continues to require its separate physical
setup unlock and temporary token.

Sources: [Home Assistant MQTT discovery](https://www.home-assistant.io/integrations/mqtt/),
[Skyfield satellite visibility](https://rhodesmill.org/skyfield/earth-satellites.html),
[Skyfield almanac](https://rhodesmill.org/skyfield/almanac.html), JPL DE421,
CelesTrak, Open-Meteo (CC BY 4.0), adsb.fi, adsbdb and OurAirports.

## Radar airports (0.12.0)

With `ENABLE_AIRPORTS=1` (default), a size-aware airport index advertises
`capabilities.airport_overlay`. Firmware requests
`/v1/airport-overlay?lat=51.5&lon=0&range=50&mode=airline&size=any`.
Ranges are 5/10/25/50/100 km; mode is airline/all; size is any/medium/large.
The response includes at most 32 airports, ordered by scheduled airline service,
size, then proximity. It is independent of map tiles and MQTT device integration.
Existing cache files upgrade automatically on the next successful dataset refresh.

## Airport approach pages (0.13.0)

`/v1/airports?lat=51.5&lon=0&mode=airline&size=any` applies the same
filters as the radar overlay and returns up to nine nearby airport items. Each
contains geographic coordinates and up to three open runways with valid endpoint
coordinates, keyed through OurAirports' stable airport identifier. Airport and
runway indexes refresh daily; the runway cache is `runways-index.json` in `/data`.
No new service, environment variable or API key is required. Missing runway data
does not prevent airport details or proximity traffic from displaying.

## Airport map radius (0.14.0)

`/v1/map` accepts `range=20` in addition to 5, 10, 25, 50 and 100 km.
Firmware uses the selected airport's latitude/longitude for its 20 km approach
map. Update the server alongside firmware to enable this map; older servers still
provide airport details, but reject the new radius. No additional capability or
configuration is needed. Maps are requested on demand and retain attribution.

## Logbook database errors

A SQLite `disk I/O error` does not by itself establish corruption. Inspect the
extended error name/code in server logs and check the volume's filesystem, mount,
permissions and free space. Do not delete the database or its journal/WAL files.
The logbook returns HTTP 503 on database errors; it never silently resets history.

After updating/rebuilding the information server, run `make docker-db-check`.
This opens `/data/sightings.sqlite3` read-only, reports storage metadata and runs
`PRAGMA integrity_check`. It does not create or repair a database. An `ok` result
only covers the fresh connection's reads, not write permissions or the existing
service connection. If that passes, restarting the service can clear a failed
long-lived connection; repeat the check and inspect logs if the issue recurs.

Before any repair, stop the service and copy the whole data directory (including
any SQLite sidecars) from the stopped container into a new backup directory:

```sh
docker compose -f info-service/compose.yaml stop photos
mkdir -p .backups
backup_dir=".backups/echoscope-data-$(date +%Y%m%d-%H%M%S)"
docker compose -f info-service/compose.yaml cp photos:/data "$backup_dir"
docker compose -f info-service/compose.yaml start photos
```

If copying fails, retain the originals and investigate the mount. Do not run
`docker compose down -v`. Repair should work on a separate copy after identifying
the error; a read-only check can also fail when a hot journal needs recovery,
which is not proof that database contents are corrupt.

`SQLITE_IOERR_GETTEMPPATH (6410)` means SQLite cannot find a writable temporary
directory. The Compose service now supplies a 64 MiB `/tmp` tmpfs and sets
`SQLITE_TMPDIR`/`TMPDIR` to it, while retaining a read-only root filesystem. Update
with `git pull` and `make docker` to recreate the container with that mount. This
fix preserves the named data volume. Larger logbook sorts can need temporary
files even when the database integrity check and small queries succeed.

## Logbook website

Open the server root (for example `http://192.168.2.89:8086/`) for the dark
logbook dashboard. `/sightings` remains an alias. Search registration, callsign,
aircraft type or hex across all retained encounters; combine it with category
and UTC date filters. Results show 50 entries per page and preserve filters
when paging. Click an aircraft to open its existing photo, route and trace view.
The collapsible photo-cache section clears only photos and returns to the site.
The site uses local styles and server-rendered forms, without a JavaScript or
external font dependency. No firmware update or database migration is required.
