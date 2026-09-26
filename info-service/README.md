# EchoScope Info Server

Optional LAN companion for aircraft photos, a faint street-map radar background, and space-station predictions. The knob continues to work with aircraft data alone if this server is absent. No provider account or API key is needed for the default sources.

## Start or upgrade

From the repository root on your Docker machine:

```sh
git pull
make docker
make docker-logs
```

It listens on **0.0.0.0:8086**. Use `INFO_PORT=8090 make docker` for another host port (`PHOTO_PORT` remains supported). `make docker-down` stops it. Existing installations keep the Compose project/service identifiers `echoscope-photos` / `photos` deliberately: upgrading replaces the old container instead of starting another one on the same port. The source directory is now `info-service/`.

On the knob, hold five seconds, browse to its displayed IP, and save `http://YOUR-SERVER-IP:8086` as **Info server URL**. Existing saved photo URLs carry over automatically. The connection test lists currently available features. Map and station capabilities refresh roughly once per minute. The map checkbox appears only when maps are offered. Leave the URL blank to disable all external features.

## Features

- **Photos:** actual aircraft thumbnail by registration via Planespotters. The photo shares flight details once loaded. Missing, disabled or unavailable photos leave the normal full text page, with no empty frame. Credits remain on-screen and original links are available at `http://KNOB-IP/photo` and `http://SERVER-IP:8086/photo/REGISTRATION`. Photographs are cached in memory for five minutes; not saved to disk.
- **Street map:** current-view OpenStreetMap tiles are reprojected to the same azimuthal equidistant projection as the radar, shaded faintly and returned as a 420×420 RGB565 image. It supports the knob's 5/10/25/50/100 km ranges. Only the requested view is fetched; no background prefetch. Attribution is visible on the scope and linked on the server home page. The background disappears immediately on a range change until the matching image is ready. Coordinates near the poles (beyond 85°) are unsupported for maps/stations.
- **Space stations:** ISS and Tiangong by default. A **SAT >** touch target appears on the radar only when fresh orbital data is available. Tap it for a north-up sky view: centre = overhead, outer circle = horizon. Rotate to select a station; tap/press to return to aircraft. Positions, azimuth, elevation, distance and the next rise above 10° in the following 24 hours are predicted using Skyfield/SGP4. Time is UTC. These are geometric predictions, not naked-eye visibility forecasts (sunlight, observer darkness and clouds are not modelled).

The map is requested only while viewing the awake radar; station position requests run only in the station view. Missing capabilities and failed station responses hide the associated UI. Temporary map failures leave the normal radar. New requests stop during display sleep. An in-flight server calculation can finish.

## Configuration

Set these environment variables when running `make docker` (retain any custom port settings used by your existing installation):

| Variable | Default | Purpose |
|---|---|---|
| `ENABLE_PHOTOS` | `1` | Set to `0` to disable photos |
| `ENABLE_MAPS` | `1` | Set to `0` to disable maps |
| `ENABLE_SATELLITES` | `1` | Set to `0` to disable station predictions |
| `SATELLITE_IDS` | `25544,48274` | Up to eight NORAD IDs from CelesTrak's **stations** group; not the full satellite catalogue |
| `INFO_PORT` | `8086` | Host port; `PHOTO_PORT` is retained as fallback |
| `TILE_URL` | OpenStreetMap standard HTTPS tiles | Optional provider template containing `{z}`, `{x}`, `{y}`; must return 256px raster tiles |
| `MAP_CREDIT` | `Copyright OpenStreetMap contributors` | On-screen map attribution; use the provider's required wording, keeping it short |

For example: `ENABLE_PHOTOS=0 ENABLE_MAPS=1 make docker`. Configuration changes require recreating the container (`make docker`). The persistent `info-cache` volume stores map tiles and orbital JSON only. The non-root process runs with a read-only root filesystem and writes only `/data`; Compose limits it to 256 MiB.

## Sources and caches

- Maps follow the [OpenStreetMap tile policy](https://operations.osmfoundation.org/policies/tiles/): identifying User-Agent, current-view requests, visible attribution and at least seven-day tile caching. There is no guaranteed availability. Change providers if your usage needs exceed the public service's policy; do not point this at Google Maps imagery without an appropriate licensed integration. Tile cache is capped at 2,048 files; fresh tiles are retained for seven days. Failed map builds back off for ten minutes. [Map copyright](https://www.openstreetmap.org/copyright) · [Report a map issue](https://www.openstreetmap.org/fixthemap).
- CelesTrak's stations group is refreshed no more often than every six hours in a running service, with a persistent cache used across restarts. Elements over seven days from their epoch are excluded; the capability disappears if no fresh configured station remains. Calculations use [Skyfield's satellite API](https://rhodesmill.org/skyfield/earth-satellites.html). [CelesTrak GP formats](https://celestrak.org/NORAD/documentation/gp-data-formats.php) · [Usage policy](https://celestrak.org/usage-policy.php).
- Photo lookups are serialized/rate-limited, use HTTPS and an identifying User-Agent, and accept bounded images only from the expected Planespotters thumbnail hosts. Attribution and original source links are required. Keep these visible if extending the service.

## Protocol

`GET /health` returns protocol 1, the display name **EchoScope Info Server**, booleans in `capabilities` (`photos`, `maps`, `satellites`) and `map_credit`. The legacy `service: "echoscope-photos"` identifier and `/v1/photo/REGISTRATION` endpoint remain for older firmware. New firmware also recognizes old photo-only servers without a capabilities object. Unknown capabilities are ignored.

- `/v1/photo/REG`: ECP1, little-endian width/height, 128-byte photographer + 256-byte HTTPS source fields, big-endian RGB565 pixels. Maximum 200×150.
- `/v1/map?lat=51.5&lon=0&range=25`: ECM1, little-endian 420×420 dimensions, then big-endian RGB565 (352,808 bytes total). HTTP 202 while the server renders asynchronously; retry later. Firmware caps the response and validates the header/dimensions.
- `/v1/satellites?lat=51.5&lon=0`: bounded JSON with generation time and up to eight stations (`name`, `id`, `az`, `el`, `km`, `next_rise`). `next_rise=0` means no qualifying rise in the next 24 hours. Firmware rejects invalid angles and stale generation timestamps.

The service is intended for your LAN. No changes to the direct adsb.fi aircraft feed are required.

## Tests

```sh
python -m pip install -r info-service/requirements.txt
python -m unittest discover -s info-service -v
```
