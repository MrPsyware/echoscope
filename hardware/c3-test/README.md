# ESP32-C3 knob hardware test

Standalone diagnostic for VIEWE UEDX24240013-MD50E: 240×240 GC9A01 IPS,
encoder GPIO 7/6 and active-low push GPIO 9. Pin 9 is also a boot strap;
release the knob during reset/upload unless deliberately entering download mode.

This is not the EchoScope Mini firmware. It does not start Wi-Fi, fetch aircraft,
write preferences or require Docker. It does not change the main ESP32-S3 build.

From the repository root:

```sh
PLATFORMIO_CORE_DIR="$PWD/.tools/platformio" .tools/venv/bin/python -m platformio run -d hardware/c3-test -e c3-hwtest -j 2
PLATFORMIO_CORE_DIR="$PWD/.tools/platformio" .tools/venv/bin/python -m platformio run -d hardware/c3-test -e c3-hwtest -t upload --upload-port /dev/ttyACM0
.tools/venv/bin/python -m serial.tools.miniterm /dev/ttyACM0 115200
```

Back up the actual device before uploading. Do not use the root `make upload`,
which currently targets the larger ESP32-S3 knob.

Check:

1. Text is upright and legible. The three bars are red, green, blue from left to right.
2. Rotate slowly one physical notch each way: check that `STEP` changes by one and the dot follows the rotation.
   Raw electrical transitions remain in serial logs: two per physical notch.
3. Single-click and double-click. `S` / `D` counts should rise once per gesture;
   single click waits 400 ms to distinguish it from a double click.
4. Hold for one second: `LONG PRESS` and `H` increment once.
5. Hold and rotate: `HOLD AND TURN`, with the step count changing and no click on release.
6. The border turns amber while pressed. The `ENCODER SKIPS` warning indicates
   an observed invalid quadrature transition; detailed counts are in serial logs.

A 7,680-byte DMA strip buffer renders synchronously at up to 10 fps. Free heap is
shown after display initialization, without Wi-Fi/TLS; it is not the eventual
application's available memory. Display transfers and heap are also logged.

References: manufacturer's schematic `Schematic/MD50E.SCH.pdf` and pinned
ESP32_Display_Panel v1.0.3 supported-board configuration.

## Initial device check (2026-10-03)

Built and uploaded successfully to the identified ESP32-C3 revision 0.4 / 4 MB
unit. Flash hashes verified. GC9A01 initialization completed and the running
firmware reported 277,360 bytes free heap after setup. Serial observations
confirmed a single click and held rotation, with zero invalid transitions in
that sample. Colour/orientation, detent direction/count and double-click/long
press still need physical confirmation. Original flash backup is retained
locally under the ignored `.backups/` directory.

## Input test v2

Button sampling runs in a separate priority-3 task every two milliseconds (or
one scheduler tick), independently of display rendering and serial output.
Debounce is 15 ms. The second press must start within 400 ms of the first release;
its release can be later. Single-click delivery waits out that window.
Clockwise movement negates the raw electrical count and two transitions make
one step. A half-notch movement no longer consumes a click as held rotation.

Host gesture checks (including bounce, double-click timing, held rotation, half
steps, long press and timer wrap):

```sh
c++ -std=c++17 hardware/c3-test/input_test.cpp -o /tmp/c3-input-test
/tmp/c3-input-test
```
