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
