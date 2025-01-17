#!/bin/bash
set -eou pipefail

# Build firmware-aio.bin
# This is the FAT bin (aka image) that is used to flash NSPanels the first time
# over serial (not OTA)
# It combines the following files:
#
# - bootloader.bin
# - partitions.bin
# - firmware.bin
# - littlefs.bin

pio_core_path="$(pipenv run platformio system info | grep "PlatformIO Core Directory" | grep -oEe "/.+$")"
boot_app0_bin_path="$pio_core_path/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

if [ ! -e "$boot_app0_bin_path" ]; then
	echo "boot_app0.bin file does not exist as $boot_app0_bin_path!"
	echo "Have you installed the esp32 platform in PlatformIO?"
	exit 1
fi

function get_partition_offset {
	partition="$1"
	partition_offset=$(grep -Eve "^#" partitions.csv | grep "$partition," | awk '{ print $4 }' | cut -d ',' -f 1)
	echo "$partition_offset"
}

echo "Building image from existing .bin-files"
pipenv run python -m esptool --chip esp32 merge_bin -o .pio/build/esp32dev/firmware-aio.bin --flash_mode dio --flash_size 4MB 0x1000 .pio/build/esp32dev/bootloader.bin 0x8000 .pio/build/esp32dev/partitions.bin 0xe000 "$boot_app0_bin_path" 0x10000 .pio/build/esp32dev/firmware.bin "$(get_partition_offset spiffs)" .pio/build/esp32dev/littlefs.bin

echo "Generate firmware-aio.bin.md5 ..."
md5sum .pio/build/esp32dev/firmware-aio.bin | cut -d ' ' -f 1 > .pio/build/esp32dev/firmware-aio.bin.md5
