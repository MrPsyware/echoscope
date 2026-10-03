# EchoScope Mini — standalone C3 prototype

For the VIEWE **UEDX24240013-MD50E**, ESP32-C3 / 4 MB flash, 240×240
GC9A01 display, rotary encoder and push button. This is a separate, experimental
firmware target. The established 466×466 ESP32-S3 firmware is unchanged.

## First use

1. Join the `EchoMini-…` Wi-Fi network shown on the knob. Its password is also shown.
2. Open `http://192.168.4.1/` if a setup page does not appear automatically.
3. Save your 2.4 GHz Wi-Fi name/password and radar latitude/longitude in decimal
   degrees. Set starting range, brightness and sleep timeout as desired.
4. The setup hotspot closes when connected. The setup screen then shows the
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
- No live trails/watchlists/photos/maps/server extras in this first prototype.
- Wi-Fi and TLS memory are measured in serial logs. Idle hardware-test memory
  must not be treated as available memory for this networked application.

## Build and upload

From the repository root:

```sh
make mini-build
make mini-test
make mini-upload PORT=/dev/ttyACM0
```

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

This prototype has no factory Wi-Fi credentials and uses its own `echo-mini`
preferences namespace. The setup hotspot has a random per-boot password. The
configuration page is intended for a trusted home network and protects saves
with a form token. No saved password is included in the page or serial logs.
