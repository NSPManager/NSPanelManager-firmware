#!/bin/bash
set -eou pipefail

# Build littlefs.bin and littlefs.bin.md5 files

echo "Building littlefs.bin ..."
pipenv run platformio run --target buildfs --environment esp32dev

echo "Generate littlefs.bin.md5 ..."
md5sum .pio/build/esp32dev/littlefs.bin | cut -d ' ' -f 1 > .pio/build/esp32dev/littlefs.bin.md5
