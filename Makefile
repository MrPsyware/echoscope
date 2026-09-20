.DEFAULT_GOAL := help
SHELL := /bin/bash

PYTHON ?= python3
PORT ?= /dev/ttyACM0
BAUD ?= 115200
ENV ?= echoscope
VENV := $(CURDIR)/.tools/venv
PY := $(VENV)/bin/python
PIO := $(VENV)/bin/pio
PLATFORMIO_CORE_DIR ?= $(CURDIR)/.tools/platformio
export PLATFORMIO_CORE_DIR
export PLATFORMIO_SETTING_ENABLE_TELEMETRY := false
export PIP_DISABLE_PIP_VERSION_CHECK := 1

.PHONY: help setup deps build test firmware upload flash flash-full monitor ports backup clean
help:
	@printf '%s\n' \
	  'EchoScope build commands:' \
	  '  make setup       Create a local Python environment and install pinned tools' \
	  '  make deps        Download the board toolchain and libraries' \
	  '  make build       Compile the ESP32-S3 firmware' \
	  '  make test        Run host model/input and JSON tests' \
	  '  make firmware    Build app/merged images and checksums in dist/' \
	  '  make upload      Build and flash app only (preserves existing settings)' \
	  '  make flash-full  Build and flash merged image (first installation)' \
	  '  make monitor     Open the serial monitor; quit with Ctrl+C' \
	  '  make ports       List available serial devices' \
	  '  make backup      Save a timestamped 16 MB backup under .backups/' \
	  '  make clean       Remove build products, retaining tools and backups' \
	  'Override serial device with PORT=/dev/ttyACM1; monitor baud with BAUD=115200.'

setup: $(VENV)/.ready
$(VENV)/.ready: requirements-build.txt
	$(PYTHON) -m venv "$(VENV)"
	"$(PY)" -m pip install -r requirements-build.txt
	@touch "$@"

deps: setup
	"$(PIO)" pkg install -e "$(ENV)"
build: setup
	"$(PIO)" run -e "$(ENV)"
test: deps
	@mkdir -p .tools/tests
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I include tests/model_test.cpp -o .tools/tests/model_test
	.tools/tests/model_test
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I include -I ".pio/libdeps/$(ENV)/ArduinoJson/src" tests/json_test.cpp -o .tools/tests/json_test
	.tools/tests/json_test
firmware: build
	"$(PY)" scripts/package_firmware.py --environment "$(ENV)"
upload: flash
flash: firmware
	"$(PY)" -m esptool --chip esp32s3 --port "$(PORT)" write_flash 0x10000 dist/echoscope-app.bin
flash-full: firmware
	"$(PY)" -m esptool --chip esp32s3 --port "$(PORT)" write_flash 0x0 dist/echoscope-merged.bin
monitor: setup
	"$(PIO)" device monitor --port "$(PORT)" --baud "$(BAUD)"
ports: setup
	"$(PIO)" device list
backup: setup
	@mkdir -p .backups
	"$(PY)" -m esptool --chip esp32s3 --port "$(PORT)" read_flash 0x0 0x1000000 ".backups/esp32s3-$$(date +%Y%m%d-%H%M%S).bin"
clean:
	$(PYTHON) scripts/clean.py
