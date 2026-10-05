# Radar visitors (original and Mini)

Development firmware `0.16.0-dev.5` has four lightweight novelty flybys.
For random appearances, put any combination of these items in the **callsign**
watchlist in web setup:

| Watch item | HTTP header value | Appearance |
|---|---|---|
| `dr evil` | `dr-evil` | Rounded rocket, radar-green outline and dim trail |
| `nyan cat` | `nyan-cat` | Animated pixel cat, pink toast body and fading rainbow |
| `santa` | `santa` | Santa's sleigh, moving reindeer and a red leading nose |
| `ufo` | `ufo` | Saucer with moving lights; hovers, shows a faint beam, then accelerates away |

Example callsign watchlist:

```text
EZY123, dr evil, nyan cat, santa, ufo
```

Watch items are case-insensitive. Multiword items require spaces: `nyan,cat` is
not the switch. These reserved items are excluded from real aircraft matching.
Removing an item cancels its current automatic appearance and prevents future
random appearances of that visitor. Manual requests remain independent.

One visitor appears at a time. Automatic appearances are scheduled roughly 1–3
hours apart, choosing randomly from the enabled visitors. Reboot starts a new
interval. Each appearance lasts 20 seconds; direction and vertical position vary.
Santa is available year-round when enabled, including for manual testing.

Leaving radar or sleeping cancels the animation, and missed appearances are not
queued. Nothing wakes the screen, keeps it awake or moves the user onto radar.
Visitors never become aircraft records, selections, alerts, MQTT data or logbook
entries. There is no music, external image download or info-server dependency.

## Manual trigger

Open the awake radar; no watchlist entry is needed. Choose the visitor using the
`X-EchoScope-Fun` header. The original Dr Evil curl command remains compatible:

```sh
curl -X POST -H 'X-EchoScope-Fun: dr-evil' http://192.168.2.151/easter-egg
curl -X POST -H 'X-EchoScope-Fun: nyan-cat' http://192.168.2.151/easter-egg
curl -X POST -H 'X-EchoScope-Fun: santa' http://192.168.2.151/easter-egg
curl -X POST -H 'X-EchoScope-Fun: ufo' http://192.168.2.151/easter-egg
```

Use the Mini's own IP address for that device. Responses: 202 accepted; 403
missing/unknown header value; 409 radar not awake/visible; 429 manual trigger
cooldown. The 30-second cooldown is shared by all visitors on a device.

This is a LAN novelty endpoint, not an authenticated device-control API. It uses
POST with a required custom header and does not enable cross-origin requests.
No other settings can be changed through it. Normal integration and firmware
update authentication remain unchanged.

All drawings are generated from shared vector/pixel primitives, scaled to both
screens and clipped to their radar circles. Host tests cover all watch markers,
HTTP names, selected automatic masks, independent manual requests, scheduling,
wraparound, duration, visibility, colours and clipping in both travel directions.
Physical appearance and frame timings still need checking on each device.
