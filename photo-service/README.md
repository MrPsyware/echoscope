# EchoScope photo service

Optional LAN service for aircraft photographs. It looks up a registration through
Planespotters.net, fetches its supplied thumbnail and converts it to RGB565 for
the knob. Photographer credit and the original-photo URL accompany every image.
Images stay in memory for five minutes (at most 32 entries), never on disk.

## Start on your Docker server

Clone the repository on your Docker server and start from its root:

```sh
git clone https://github.com/MrPsyware/echoscope.git
cd echoscope
make docker
```

`make docker` builds and starts the service in the background, bound to
**0.0.0.0:8086**. Use `make docker-logs` to follow logs and `make docker-down` to
stop it. Docker Engine and the Compose plugin are required; firmware tools are
not needed. To choose another port: `PHOTO_PORT=8090 make docker`.

Alternatively, copy this directory alone to your server and run inside it:

```sh
docker compose up -d --build
docker compose logs -f
```

Port 8086 is published by default. Set `PHOTO_PORT` to change the host port.
Use this on your trusted LAN, not as a public internet service. The container
runs without root, with a read-only filesystem and a 128 MB memory limit.
Stop it with `docker compose down`. There are no persistent data volumes.

## Configure the knob

1. Flash the matching EchoScope firmware.
2. Hold the knob for 1.5 seconds to unlock setup.
3. Open the knob's LAN IP in a browser, or use its setup Wi-Fi.
4. Enter `http://YOUR-DOCKER-SERVER-IP:8086` in **Photo service URL**.
   Do not use localhost: that would refer to the knob itself.
5. Choose **Test connection**, then save. Blank disables photos.
6. Open an aircraft's details and tap **PHOTO >**. Press or tap to return to details.

The connection test verifies the service/protocol, not upstream availability.
Missing registrations or missing photos show an unavailable message. Opening
`http://KNOB-IP/photo` shows the credit and original link for its last loaded
photograph. The service also provides `/photo/REGISTRATION` for that purpose.

The service URL currently supports plain HTTP on your LAN, without a path,
credentials or IPv6 literal. HTTPS to the photo provider is verified by Python.
Aircraft updates take priority over starting photo requests; ongoing requests
have bounded timeouts. Responses for an aircraft you have left are discarded.
Photo requests pause during standby.

## Endpoints

- `GET /health`: service identification and protocol version.
- `GET /v1/photo/REGISTRATION`: image packet, or 404 if no photo exists.
- `GET /photo/REGISTRATION`: photographer credit and original-photo hyperlink.

Protocol 1 begins with `ECP1`, little-endian uint16 width and height, then
NUL-terminated UTF-8 photographer (128 bytes) and source URL (256 bytes).
Remaining bytes are big-endian RGB565 pixels, matching the device's swapped
16-bit LVGL format. Maximum dimensions are 200 × 150. Aspect ratio is preserved
and images are never enlarged.

Upstream requests use an identifying User-Agent, verified HTTPS, an image-host
allowlist, bounded response sizes, no redirects and at most one new lookup per
second. There is no API key. Access is subject to provider availability and its
current [photo API terms](https://www.planespotters.net/photo/api). Keep the
supplied photographer credit and original-photo link with displayed images.
The API documentation page was blocked during development; persistent disk
caching is intentionally avoided.

## Tests

With requirements installed, run `python -m unittest -v test_service` here.
The tests exercise pixel format, sizing, attribution, invalid images, URL
allowlisting and negative caching. The Docker image contains only the runtime
files; tests can be mounted read-only for execution.
