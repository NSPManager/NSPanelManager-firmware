SCRIPT_DIR := scripts
.PHONY: build-firmware build-littlefs build-aio clean upload monitor install pre-commit

# Print help
help:
	@awk '/^#/{c=substr($$0,3);next}c&&/^[[:alpha:]][[:alnum:]_-]+:/{print substr($$1,1,index($$1,":")),c}1{c=0}' $(MAKEFILE_LIST) | column -s: -t

# Build the firmware
build-firmware:
	@echo "Building firmware..."
	@bash $(SCRIPT_DIR)/build-firmware.sh

# Build the littlefs file
build-littlefs:
	@echo "Building littlefs..."
	@bash $(SCRIPT_DIR)/build-littlefs.sh

# Build the aio firmware
build-aio: build-firmware build-littlefs
	@echo "Building firmware-aio.bin..."
	@bash $(SCRIPT_DIR)/build-aio.sh

# Clean build files
clean:
	@echo "Cleaning the project..."
	@pipenv run platformio run --target clean
	@rm -rf .pio  # Remove additional build artifacts

# Upload firmware to the device
upload: build-firmware
	@echo "Uploading the firmware..."
	@pipenv run platformio run --target upload

upload-aio: build-aio
	@echo "Flash NSPanel with the all in one firmware..."
	@bash $(SCRIPT_DIR)/flash-aio.sh

# Open serial monitor
monitor:
	@echo "Opening serial monitor..."
	@pipenv run platformio device monitor

# Install dependencies
install:
	@echo "Installing dependencies..."
	@pipenv install --ignore-pipfile

build-compile-commands:
	@echo "Build compile commands..."
	@bash $(SCRIPT_DIR)/build-compile-commands.sh

# Run sanity checks
pre-commit:
	@echo "Run pre-commit checks..."
	@pipenv run pre-commit run --all-files
