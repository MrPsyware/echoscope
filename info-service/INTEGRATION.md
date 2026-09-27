# Home Assistant, pickup and observing

EchoScope 0.9.0 uses the Info Server as a bridge. The knob keeps its small local
HTTP control API; Docker does MQTT, astronomy, background flight lookups and
SQLite history. One Info Server supports one knob. You do not need MQTT to use
the spotting log or observing/pickup alerts.

## Setup

1. Update the knob firmware and this Docker service to 0.9.0 or newer.
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
| `set/page` | `radar`, `aircraft`, `route`, `information`, `stations`, `weather`, `family`, `airports`, `stargazing`, `highlights` (available pages only) |
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
preferred boot brightness in web setup. Explicit sleep disarms pickup and pauses
the aircraft feed, but HTTP controls stay available to wake the device. Controls
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

With device integration connected, INFO gains **Today's highlights** (latest eight
interesting aircraft). `/sightings` on the Info Server shows paged full history.
Records include registration, callsign, type, reason, first/last seen and closest
observed distance. Watchlist matches, military and helicopters are included from
the received feed; demo/stale positions are excluded. Idle sleep stops local
sighting collection. It cannot recover aircraft absent from the knob's feed.

Data is stored in `/data/sightings.sqlite3` in the existing Docker named volume.
Retention is 30 days and at most 10,000 aircraft/day records; days use UTC. It groups
each aircraft's observations for the day, not individual takeoffs/landings. The
history page is a read-only LAN page and escapes aircraft/provider strings.

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
