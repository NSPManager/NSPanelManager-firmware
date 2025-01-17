#!/bin/bash
set -eou pipefail

echo "Trying to flash NSPanel with all in one firmware via /dev/ttyUSB1"
pipenv run python -m esptool --baud 921600 --port /dev/ttyUSB1 write_flash 0x0 .pio/build/esp32dev/firmware-aio.bin
