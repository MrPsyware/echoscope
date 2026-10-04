# Standalone original EchoScope — development branch

Branch: `dev/standalone-pro`. Firmware: `0.16.0-dev.1`.
This is development firmware for the **original ESP32-S3**, not a published
release. Mini behaviour is unchanged. The stable release remains 0.15.0.

## Feature sources

| Feature | Without an info server | With a capable info server |
|---|---|---|
| Aircraft, trails, filters, watch alerts | Existing direct adsb.fi feed | Same |
| Airport markers / airport details / 20 km approach | Built-in worldwide large scheduled-service airports, with runway geometry where available, plus custom entries | Server airport dataset and filters |
| Aircraft route page | Direct adsbdb lookup | Server lookup |
| Weather / clouds | Direct Open-Meteo: next hour, today, next three days and next-night cloud mean, labelled UTC | Existing server weather pages with local times |
| Radar / airport map | On-demand OSM tiles, persistent device cache, local projection | Server-rendered map |
| Photos, family-flight tracking and pickup, stargazing page, space stations, logbook | Hidden | Available if advertised by server |

There is no mode switch. Each feature prefers the server when advertised;
missing server capabilities use the local implementation. Health is checked
periodically, and failed weather, route or airport requests can fall back locally.
Cloud forecasts remain available through Weather without the server's separate
Stargazing page. Route database information is not a flight plan, schedule or ETA.

## Setup

Hold the knob for five seconds and open its setup URL. Leave **Info server URL**
blank for standalone operation. The Maps and airports section has the map switch,
marker appearance, the built-in airport switch and custom airport textarea.

Custom entries use `CODE: longitude/latitude`, one per line, up to 32:

```text
LGW: -0.185739/51.148744
JFK: -73.7781/40.6413
```

Custom codes replace a matching built-in IATA/ICAO entry. Custom entries have no
runway geometry. Turn off the built-in list for a custom-only list. Size/airline
filters apply to the server dataset; standalone entries are the large scheduled
pack plus the explicitly supplied custom entries. The built-in pack currently
contains 1,153 airports worldwide. The nearby airport menu shows up to nine within
300 km; opening a marker explicitly pins that airport into the menu.

## Map storage and behaviour

Radar starts without waiting for maps. “Preparing map” reports local preparation.
Only tiles intersecting the current visible circular viewport are requested;
there is no range prefetch. Work advances between live aircraft polls, while the
UI runs independently. Sleep stops map work. Tile downloads use verified HTTPS
and an identifying User-Agent. Attribution remains visible on a displayed map.

The existing 3.375 MiB data partition holds a LittleFS tile cache with a conservative
2.64 MiB budget, reserving space for filesystem overhead and atomic downloads.
An erased partition is initialized automatically. A nonempty, unmountable
partition is **not** formatted; radar still works with “Map cache unavailable”.
OTA preserves the partition. Compressed PNG tiles persist across reboot; only
the currently prepared map and projection lookups live in PSRAM. Reopening a
cached viewport reconstructs it without downloading fresh tiles again.

Tiles are retained for at least seven days. Only expired entries are pruned.
If the cache cannot fit another bounded download, the map stops with a cache-full
message rather than evicting fresh tiles and repeatedly downloading them. Network,
storage or decoder failure leaves the radar usable and retries later. The initial
tile provider is the OSM standard raster service; there is no provider setting yet.
Maps near the Web Mercator polar limit are unavailable.

Policy: https://operations.osmfoundation.org/policies/tiles/
This implementation follows the on-demand viewport/cache approach, not offline
region downloads. Public provider availability and terms apply; commercial
products should review provider terms before distribution. [Open-Meteo's free hosted endpoint](https://open-meteo.com/en/terms) is intended
for non-commercial use.

## Data and implementation

- Airport source: https://ourairports.com/data/ (public domain). Regenerate the
  committed pack with `python scripts/update_airports.py`; ordinary builds do
  not download airport data. The script retains up to three longest open runways.
- Weather: https://open-meteo.com/en/docs (CC BY 4.0). Normalized weather is cached
  in PSRAM for 15 minutes; failed requests are throttled.
- Routes: https://www.adsbdb.com/ . Eight recent lookups are cached for six hours,
  with shorter negative caching. Registrations/unresolved callsigns say unavailable.
- PNG decoding: LodePNG bundled with the pinned LVGL dependency; its original
  license remains in that dependency. Allocations use bounded PSRAM allocations.
- TLS adds ISRG X1/X2 and GlobalSign Root R3 alongside existing GTS roots. Hostname
  and certificate verification remain enabled.

## Validation before promotion

Run `make test` and `make firmware`. The host suite covers airport parsing,
nearest/pinned selection, custom overrides, missing weather values, forecast page
shape, route fallbacks, map coordinates and dateline wrapping.

Physical-device testing is still required: initial cache creation; fresh and cached
maps on all radar ranges; approach maps; render/input responsiveness during TLS;
weather and route pages; setup save; sleep/wake; and server loss/recovery. Check
serial/Wi-Fi logs for internal heap, map failure messages and feed cadence. Do not
promote this branch to an official release until those checks pass.
