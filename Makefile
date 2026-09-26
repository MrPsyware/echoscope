.DEFAULT_GOAL := help
SHELL := /bin/bash

PYTHON ?= python3
DOCKER ?= docker
PORT ?= /dev/ttyACM0
BAUD ?= 115200
IP ?=
export IP
ENV ?= echoscope
VENV := $(CURDIR)/.tools/venv
PY := $(VENV)/bin/python
PIO := $(VENV)/bin/pio
PLATFORMIO_CORE_DIR ?= $(CURDIR)/.tools/platformio
export PLATFORMIO_CORE_DIR
export PLATFORMIO_SETTING_ENABLE_TELEMETRY := false
export PIP_DISABLE_PIP_VERSION_CHECK := 1

.PHONY: help setup deps build test firmware upload flash flash-full monitor ports backup clean docker docker-down docker-logs
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
	  '  make upload IP=192.168.2.151   Wireless app update (unlock setup first)' \
	  '  make monitor IP=192.168.2.151  Follow application logs over Wi-Fi' \
	  '  make ports       List available serial devices' \
	  '  make backup      Save a timestamped 16 MB backup under .backups/' \
	  '  make docker      Build/start the info server on 0.0.0.0:8086' \
	  '  make docker-down Stop the info server' \
	  '  make docker-logs Follow info server logs' \
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
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I include tests/photo_test.cpp -o .tools/tests/photo_test
	.tools/tests/photo_test
	"$(PY)" tests/network_device_test.py
firmware: build
	"$(PY)" scripts/package_firmware.py --environment "$(ENV)"
ifneq ($(strip $(IP)),)
upload: firmware
	"$(PY)" scripts/network_device.py upload
else
upload: flash
endif
flash: firmware
	"$(PY)" -m esptool --chip esp32s3 --port "$(PORT)" write_flash 0xe000 "$(PLATFORMIO_CORE_DIR)/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" 0x10000 dist/echoscope-app.bin
flash-full: firmware
	"$(PY)" -m esptool --chip esp32s3 --port "$(PORT)" write_flash 0x0 dist/echoscope-merged.bin
monitor: setup
ifneq ($(strip $(IP)),)
	"$(PY)" scripts/network_device.py monitor
else
	"$(PIO)" device monitor --port "$(PORT)" --baud "$(BAUD)"
endif
ports: setup
	"$(PIO)" device list
backup: setup
	@mkdir -p .backups
	"$(PY)" -m esptool --chip esp32s3 --port "$(PORT)" read_flash 0x0 0x1000000 ".backups/esp32s3-$$(date +%Y%m%d-%H%M%S).bin"
clean:
	$(PYTHON) scripts/clean.py

docker:
	$(DOCKER) compose -f info-service/compose.yaml up -d --build
docker-down:
	$(DOCKER) compose -f info-service/compose.yaml down
docker-logs:
	$(DOCKER) compose -f info-service/compose.yaml logs -f
