#!/bin/bash
set -eou pipefail

# Build firmware.bin and firmware.bin.md5 files

echo "Building firmware.bin ..."
pipenv run platformio run --environment esp32dev

echo "Generate firmware.bin.md5 ..."
md5sum .pio/build/esp32dev/firmware.bin | cut -d ' ' -f 1 > .pio/build/esp32dev/firmware.bin.md5
