#!/bin/bash
# Build and flash quantum-watch firmware to the device.
# Requires: ESP-IDF v5.4.x with export.sh already sourced.
#
# Usage:
#   . $IDF_PATH/export.sh   # in same shell, or add to your profile
#   ./deploy.sh
#   ./deploy.sh /dev/cu.usbmodem1101   # specify port

set -e
cd "$(dirname "$0")/firmware/esp-brookesia"

PORT="${1:-/dev/cu.usbmodem1101}"

if ! command -v idf.py &>/dev/null; then
    echo "Error: idf.py not found. Source ESP-IDF first:"
    echo "  . \$IDF_PATH/export.sh"
    echo ""
    echo "This project requires ESP-IDF v5.4.x"
    exit 1
fi

echo "Building..."
idf.py build

echo "Flashing to $PORT..."
idf.py -p "$PORT" flash

echo "Done. You can run 'idf.py -p $PORT monitor' to view logs."
