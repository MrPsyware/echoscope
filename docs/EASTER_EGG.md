# Unidentified contact (original and Mini)

Development firmware `0.16.0-dev.4` includes a novelty rocket flyby with independent manual and automatic triggers.
For automatic appearances, in web setup add the literal two-word item **dr evil** to the **callsign** watchlist:

```text
EZY123, dr evil, BAW123
```

Case does not matter. Keep a space between the words; `dr,evil` is not the switch.
The marker is saved but excluded from normal aircraft matching. Removing it stops an automatic appearance and disables future random ones.
Manual HTTP appearances work independently of this marker. No server, external image, audio or extra download is used.

While the radar is awake, the rocket crosses the scope for 20 seconds with an
UNIDENTIFIED label and a dim trail. Direction and vertical position vary. Automatic
appearances are scheduled roughly 1–3 hours apart; reboot starts a new interval.
Leaving radar or sleeping cancels a flyby, and missed appearances are not queued.
The effect does not wake the screen, keep it awake or move the user onto radar.
It is never inserted into aircraft data, selections, alerts, MQTT or logbook entries.

For an immediate appearance, open the awake radar; no watchlist entry is needed:

```sh
curl -X POST -H 'X-EchoScope-Fun: dr-evil' http://192.168.2.151/easter-egg
# Mini: use its own address instead, e.g. http://192.168.2.88/easter-egg
```

Responses: 202 accepted; 403 missing/incorrect header; 409 radar not awake/visible;
429 manual trigger cooldown (30 seconds). This is a LAN novelty endpoint,
not an authenticated device control API. It uses POST with a required custom header
and does not enable cross-origin requests. No other settings can be changed through
it. Normal integration and firmware-update authentication remain unchanged.

One shared, allocation-free vector animation is scaled to both screens and clipped
to their radar circles. Host tests cover parsing, excluding the secret marker from
real watches, random schedule bounds, wraparound, duration, visibility and clipping.
On-device visual/timing checks remain necessary before a stable release.
