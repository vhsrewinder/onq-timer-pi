#!/bin/bash

echo "======================================="
echo "   ESP32 SPIFFS Upload (Config-based)"
echo "======================================="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/esp32_spiffs_config.json"

echo "Checking for configuration file..."
if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: Configuration file not found: $CONFIG_FILE"
    echo "Please create esp32_spiffs_config.json in your project directory"
    echo
    exit 1
fi

echo "Configuration file found: $CONFIG_FILE"
echo

echo "Reading configuration and uploading SPIFFS..."
python3 "$SCRIPT_DIR/esp32_spiffs_upload.py" --config "$CONFIG_FILE"

if [ $? -eq 0 ]; then
    echo
    echo "======================================="
    echo "     SPIFFS Upload Successful!"
    echo "======================================="
else
    echo
    echo "======================================="
    echo "       SPIFFS Upload Failed!"
    echo "======================================="
fi

echo