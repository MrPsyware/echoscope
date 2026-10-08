# Phone setup and watchlist management

Development firmware **0.17.0-dev.1**, on `dev/mobile-setup`, for both knobs.
The published release remains 0.16.0. No companion app, account or Docker service
is required for this interface.

## Open it on a phone

Hold the knob still for five seconds. When connected to home Wi-Fi, Setup shows
a QR code for the knob's current local website. Scan with the phone camera; the
phone must be on the same network. The address and network name remain visible.
The original still requires its physical Setup unlock to access/save settings.

Without a working network connection, Setup shows a Wi-Fi QR to join its setup
hotspot. **Rotate the knob** to switch between the Wi-Fi QR and website QR. Join
the hotspot first, then scan/open the website. Press to leave Setup as before.
Codes are generated locally, with a four-module white quiet zone; no QR service
receives the hotspot password.

## First-time setup

1. Enter the 2.4 GHz home Wi-Fi name and password, then choose **Connect Wi-Fi
   first**. This saves only Wi-Fi, without requiring invented coordinates.
2. The knob connects (Mini restarts), then shows its new website QR. Put the phone
   back on the same home Wi-Fi and scan that QR. The hotspot stops after connection.
3. Search for a town and country, or a full UK postcode. Choose the correct
   result, optionally open the confirmation map, then **Save settings**. Radar
   location is not guessed from an ambiguous search result.
4. Add watches if desired. Press the knob to return to radar.

Search runs in the phone browser over HTTPS: town searches use Open-Meteo/GeoNames;
full UK postcodes use Postcodes.io. Only explicit searches contact those providers.
The optional confirmation map uses OpenStreetMap and shares the selected
coordinates when opened. Search/map need internet access; manual latitude and
longitude entry remains under Advanced. GPS location permission is not requested
because the device's HTTP page is not a secure browser context.

## Everyday use

- **Watchlist:** add an aircraft type, registration or broadcast callsign; give it
  a friendly name, switch it on/off, edit or delete it. The type picker includes
  common aircraft names and uses `A388` for Airbus A380. Labels and disabled
  entries survive saving and reboot. Existing raw watches are imported.
- **Save watchlist** updates only watch identifiers and editor metadata. It does
  not reconnect Wi-Fi, reset the device or change location/display settings.
  Press the knob to return to radar after saving; Mini pauses feed polling in Setup.
- **Display:** brightness, sleep, starting range and existing watch-alert settings.
- **Settings:** Wi-Fi and radar location, plus Advanced for the remaining existing
  options, including original-only server features. Saving full settings retains
  Mini's existing restart behaviour.

Up to 48 editor rows are supported. The original retains its limit of 16 active
identifiers per category and trailing `*` prefix matching. Mini retains exact
matching and a 95-character limit per active category. Disabled rows do not
participate in matching. Radar visitors remain valid callsign-watch items.

This stage does **not** resolve booking numbers, provide worldwide family-flight
tracking or add date-based expiry. Use the broadcast callsign shown on the knob;
a friendly name is only a label. Watches operate within each device's normal
received/retained aircraft coverage.

## Development checks

Shared UI sources: `web/setup.js` and `web/setup.css`. Run
`python scripts/embed_setup.py` after edits; `make test` checks that embedded
assets are current and tests watch metadata/QR generation.

Browser regression tests use mocked device/location responses and exercise both
mobile layouts, add/edit/delete/disable/save/reload, preservation of unrelated
settings, Wi-Fi-only setup, postcode/town lookup, offline errors and map preview:

```sh
python -m pip install playwright==1.63.0
python -m playwright install chromium
python tests/mobile_setup_browser_test.py
# Or use an existing browser:
python tests/mobile_setup_browser_test.py --chromium /usr/bin/chromium
```

The same browser suite runs in GitHub Actions. Physical camera scanning on both
knobs and captive-network/iPhone behaviour still need device testing.
