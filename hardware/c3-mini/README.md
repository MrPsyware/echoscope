# EchoScope Mini — standalone C3

For the VIEWE **UEDX24240013-MD50E**, ESP32-C3 / 4 MB flash, 240×240
GC9A01 display, rotary encoder and push button. **Release 0.15.0.** This is a
separate firmware target from the 466×466 ESP32-S3 original.

## First use

1. Join the `EchoMini-…` Wi-Fi network shown on the knob. Its password is also shown.
2. Open `http://192.168.4.1/` if a setup page does not appear automatically.
3. Save your 2.4 GHz Wi-Fi name/password and radar latitude/longitude in decimal
   degrees. Set starting range, brightness and sleep timeout as desired.
4. Save restarts the Mini after storing your settings, then returns to radar
   automatically. The setup hotspot closes when connected. The setup screen then shows the
   configured network and the knob's LAN IP. Click to view radar.

Wi-Fi and location persist across restarts. Hold the knob for five seconds to
return to setup. If the configured network cannot be reached for 20 seconds,
the setup hotspot returns. The browser configuration remains available at the
LAN IP. The Mini contacts adsb.fi directly; **no Docker server is required**.

## Controls

- Rotate: cycle aircraft, or adjust range in Range mode (5 / 10 / 25 / 50 km).
- Hold and rotate on radar: alternate Aircraft / Range modes.
- Click an aircraft selection: open its details.
- Rotate in details: change aircraft; hold and rotate: change detail page.
- Double-click: return to radar, or open the menu from radar.
- Menu: rotate and click to select Radar, Setup or Sleep.
- Hold for five seconds without turning: setup.
- Any interaction while asleep: wake, consuming that wake gesture.
- Sleep switches off both the LCD image and its backlight. The active-low GPIO8
  backlight polarity is corrected from the pinned vendor profile, so brightness
  now increases with the configured percentage. This is display standby, not
  ESP32 deep sleep.

Button sampling is independent of drawing/networking. Two electrical encoder
transitions make one clockwise-positive notch. Single-click action waits 400 ms
to distinguish a double-click. Position and flight detail pages use separate
small layouts, not downscaled versions of the large knob.

## Data and memory

- Exclude aircraft explicitly marked `alt_baro: "ground"`; numeric low/zero
  altitudes remain eligible, preserving airborne arrivals and departures.
- Decode one aircraft object at a time, retain the nearest 32 valid positions
  within the selected radius. The footer marks when only the nearest subset fits.
- Poll around every five seconds after a completed request, with error backoff
  and at least one minute for HTTP 429. Sleeping/setup pauses feed polling.
- Verified TLS and hostname checking use the existing project's compact TLS
  configuration and GTS CA roots. Clock synchronization is required.
- Keep the last good snapshot on failed/truncated/malformed replies. Mark old
  data stale at 30 seconds; hide radar aircraft after 60 seconds without updates.
- Display uses a 7,680-byte strip buffer. JSON framing uses bounded object and
  envelope buffers (6 KiB / 2 KiB); never accumulates the full body. HTTPClient
  handles chunked transfer decoding. Maximum response 1 MiB, bounded read timeout.
- Recent trails retain up to 32 moving positions for each retained aircraft
  (12,932 bytes total), in RAM only. Departed aircraft release their history;
  gaps over 30 seconds start a new trail. Trail segments keep their recorded
  altitude colour; the selected aircraft has a brighter trail and orange icon.
- Watchlists match exact type codes, registrations or flight callsigns, ignoring
  case. Separate entries with spaces or commas, up to 95 characters per field.
  For an A380, use **A388**. A match among the nearest 32 aircraft pulses a green
  outer ring while its position is fresh; ring brightness is configurable, with
  0 disabling the ring. Details identify watched aircraft. Alerts do not run
  while sleeping or in setup, because those modes pause polling.
- Airport markers need no server. The default list contains 17 UK airports
  classified as large with scheduled service by [OurAirports](https://ourairports.com/data/),
  retrieved 2026-10-04 (public-domain data). Codes and subdued square icons sit
  behind planes and trails. Only airports within the selected range are shown.
- Web setup offers Off / Built-in UK / Custom airports. A custom list replaces
  the default list; switch back to Built-in UK to restore it. Up to 32 unique
  airport codes (2–8 letters/digits), with decimal **longitude/latitude**, e.g.:

  ```text
  LGW: -0.185739/51.148744
  LHR: -0.459909/51.470748
  ```

  One airport per line. Invalid/duplicate/out-of-bounds entries reject the save
  without replacing the existing configuration. Watchlists, airport settings
  and the trail display toggle persist alongside the existing display settings.
- No photos, map tiles or info-server extras in this standalone Mini.
- Wi-Fi and TLS memory are measured in serial logs. Idle hardware-test memory
  must not be treated as available memory for this networked application.

## Build and upload

From the repository root:

```sh
make mini-build
make mini-test
make mini-firmware  # package images and checksums in dist/mini/
make mini-upload PORT=/dev/ttyACM0
```

[GitHub Releases](https://github.com/MrPsyware/echoscope/releases/tag/v0.15.0)
provides `echoscope-mini-app-0.15.0.bin` for updates and
`echoscope-mini-merged.bin` for first installation, plus `MINI-SHA256SUMS`.
For downloaded images, using the project's Python environment:

```sh
# Existing Mini installation: preserve settings
.tools/venv/bin/python -m esptool --chip esp32c3 --port /dev/ttyACM0 write_flash 0x10000 echoscope-mini-app-0.15.0.bin
# First installation only: overwrites the configuration area
.tools/venv/bin/python -m esptool --chip esp32c3 --port /dev/ttyACM0 write_flash 0x0 echoscope-mini-merged.bin
```

Use the actual USB port shown by `make ports`; it may be `/dev/ttyACM1`.
Never flash an original ESP32-S3 image to the Mini.

The upload target is explicitly ESP32-C3. Root `make upload` still targets the
ESP32-S3 and must not be used for this knob. `mini-upload` is USB only; wireless
updates are not implemented yet. The 4 MB partition layout reserves two 1.875 MiB
application slots and keeps preferences in NVS, without a filesystem.
Back up original firmware before first installation. Do not hold the knob down
when resetting unless intentionally entering its ROM download mode (GPIO 9).

Serial diagnostics:

```sh
.tools/venv/bin/python -m serial.tools.miniterm /dev/ttyACM0 115200
```

The Mini has no factory Wi-Fi credentials and uses its own `echo-mini`
preferences namespace. The setup hotspot has a random per-boot password. The
configuration page is intended for a trusted home network and protects saves
with a form token. No saved password is included in the page or serial logs.

## Resource check (2026-10-04)

This build occupies 1,280,436 / 1,966,080 application bytes (65.1%); static RAM
is 68,036 bytes. A short live run with 2–3 aircraft completed seven verified
HTTPS requests successfully. Free heap settled near 139,020 bytes between
requests, with a minimum of 68,472 bytes during TLS; steady radar drawing took
75–76 ms within the 200 ms frame period. This is a light-traffic measurement,
not a full-capacity endurance test. Trails use fixed storage regardless of
how long the Mini is running.
