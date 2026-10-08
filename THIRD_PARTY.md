# Third-party components

The original EchoScope (formerly Sky Knob) application is MIT licensed. The following retain their own licences:

- `src/lvgl_v8_port.cpp` and `include/lvgl_v8_port.h`: Espressif, CC0-1.0, copied from the VIEWE example at commit `15fbde3e9c46986bdb85414a1c5f3cee3075ffc5`. SPDX notices retained.
- `include/lv_conf.h` and `boards/ESP-LCD.json`: configuration based on the same MIT-licensed VIEWE repository. The repository licence is in `licenses/VIEWE-LICENSE`.
- Espressif ESP32_Display_Panel 1.0.3, ESP32_IO_Expander 1.1.0 and esp-lib-utils 0.2.0: Apache-2.0; retrieved by PlatformIO from the repositories pinned in platformio.ini.
- LVGL 8.4.0: MIT; https://github.com/lvgl/lvgl/tree/v8.4.0
- ArduinoJson 7.4.2: MIT; https://github.com/bblanchon/ArduinoJson/tree/v7.4.2
- Arduino-ESP32 3.1.1 and ESP-IDF components retain their LGPL/Apache and component-specific licences. Source: https://github.com/espressif/arduino-esp32/tree/3.1.1 and the pinned platform toolchain.
- Root certificates: Google Trust Services Roots R1 and R4, obtained from https://pki.goog/repo/certs/gtsr1.pem and https://pki.goog/repo/certs/gtsr4.pem.

MatixYo/ESP32-Plane-Radar was consulted for its public feed URL and behaviour. No application source was copied from that project.

- `include/vendor/stb_image.h`: stb_image 2.30, Sean Barrett and contributors, used under the MIT option in its embedded dual licence. Unmodified file from https://github.com/nothings/stb/blob/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h ; SHA-256 `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`. Only the JPEG decoder is compiled, with bounded dimensions and PSRAM allocators on ESP32-S3.
- Aircraft photo thumbnails: Planespotters.net and their respective photographers. The photographer credit and original-photo link travel with every thumbnail; no third-party photos are included in the repository's synthetic decoder test fixtures.

- `include/vendor/qrcodegen.c` and `.h`: Project Nayuki QR Code generator, MIT licence retained, pinned at https://github.com/nayuki/QR-Code-generator/tree/3c6d0b3cefb4e049dc337e82237c9644399716a8/c . Used for locally generated setup QR codes on both devices.
- Optional phone location search uses Open-Meteo/GeoNames (https://open-meteo.com/en/docs/geocoding-api) and Postcodes.io (https://postcodes.io/docs/postcode/lookup/). Confirmation maps are embedded from OpenStreetMap with attribution.
