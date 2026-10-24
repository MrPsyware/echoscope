# Rain radar (original EchoScope)

Available in development firmware 0.17.0-dev.2; server offload in 0.17.0-dev.3. The Mini is unchanged.

Open **Information → Rain radar**. Turn the knob to change the radius (5, 10,
25, 50 or 100 km). Tap **< / >**, swipe, or hold-and-turn to move through the
available past frames. Press the knob or tap the centre to return to aircraft.
The initial frame is the latest; while on the latest frame, new frames are
selected automatically. Selecting history keeps that time until it leaves the
provider's history window. The aircraft view and rain view share the range.

The map uses the existing faint-map setting, with the info-server map when
available and cached standalone OSM tiles otherwise. Rain radar automatically uses an info server advertising `rain_radar`; otherwise
it connects directly to RainViewer. No account or API key is needed.
It still works over the range grid when maps are disabled or unavailable.

The image uses RainViewer's Universal Blue colours: pale blue through blue,
yellow/orange and red for progressively stronger radar echoes. The legend is
qualitative: these are radar reflectivity observations, not measured rainfall at
ground level. No future prediction is shown. Hatched areas are outside the
provider's coverage mask; an unhatched area is not a guarantee of current local
observations. Each image shows its frame timestamp in UTC and distinguishes history from the
latest frame. The view is labelled STALE when even the latest available frame is
more than 45 minutes old. Individual contributing radars can be older than
the composite frame. Failed refreshes retain the last complete image with a
visible error, never a partly downloaded image.

[Weather data by RainViewer](https://www.rainviewer.com/).
The [current API](https://www.rainviewer.com/api/weather-maps-api.html) provides
about two hours of past frames at ten-minute intervals. Its
[2026 limits](https://www.rainviewer.com/api/transition-faq.html) include maximum
zoom 7, Universal Blue only and 100 requests/IP/minute. Zooming in enlarges those
available pixels; it does not add street-level resolution. The free service is
intended for personal/educational use and has no availability guarantee.

## Optional server offload

Update the Docker server and original firmware to the current development build.
Keep the existing **Info server URL** in web setup. The server advertises rain
radar automatically (`ENABLE_RAIN=1`, the default); discovery takes up to a minute.
Older servers continue to use standalone rendering without errors.

The server downloads and caches tiles, handles PNG decoding and geographic
stitching, then sends one ready-to-draw 420×420 overlay across the LAN. It keeps
up to 16 results (about 8 MiB) and 64 decoded tiles (16 MiB) in bounded RAM caches.
Weather frames expire after three hours, coverage after one day, and metadata
refreshes every five minutes. Restarting the container clears these rain caches;
the existing photo/map disk caches are unaffected. Only requested views are built.

A cold request returns HTTP 202 while one background worker prepares it; the
knob retries once a second. A cached frame is returned immediately, although the
529 KB LAN transfer still takes time. First loads remain dependent on upstream
latency. Server errors, invalid packets, or preparation taking over 45 seconds
switch the knob back to standalone for two minutes. Upstream HTTP 429 pauses
both paths for two minutes rather than retrying via the same public IP.

## Standalone resource and network behaviour

- Only the visible rain view downloads data; sleep and leaving the view cancel
  unfinished work. Aircraft polling continues with priority between steps.
- Metadata refreshes every five minutes. Image/coverage requests are spaced at
  least 1.1 seconds apart; failures back off a minute and HTTP 429 at least two
  minutes. Other devices on the same public IP share RainViewer's limit.
- One complete frame is retained in PSRAM, together with the current range's
  coverage mask. History frames are fetched on demand, not animated or prefetched.
  Revisited older frames may need downloading again. No weather tiles are written
  to flash, while the optional server has its own bounded in-memory cache.
- Peak added pixel/lookup/compressed buffers are roughly 3 MB in PSRAM (including PNG decoder workspace) during a
  refresh, including the previous displayed frame. Steady image plus coverage
  storage is about 690 KiB. Allocation failures leave a visible error and retry.
- Images are bounded to 256 KiB compressed and checked as 256×256 PNGs before
  decoding. TLS certificates and hostnames remain verified. Map and radar use
  the same geographic projection, including longitude wrapping at the date line.

The authenticated device API advertises the page as `rain` and accepts
`{"page":"rain"}` through its existing page control.

Hardware validation is still needed for display appearance and fetch latency on
the actual knob. No firmware is uploaded automatically by building this feature.
