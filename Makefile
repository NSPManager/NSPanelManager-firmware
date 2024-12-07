# Makefile for PlatformIO with Pipenv

# Default target
all: build

# Run a build
build:
	@echo "Building the project..."
	pipenv run platformio run

# Clean build files
clean:
	@echo "Cleaning the project..."
	pipenv run platformio run --target clean

# Upload firmware to the device
upload:
	@echo "Uploading the firmware..."
	pipenv run platformio run --target upload

# Open serial monitor
monitor:
	@echo "Opening serial monitor..."
	pipenv run platformio device monitor

# Install dependencies
install:
	@echo "Installing dependencies..."
	pipenv install --ignore-pipfile
