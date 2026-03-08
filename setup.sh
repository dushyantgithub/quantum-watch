#!/bin/bash
# Build esp-brookesia (Factory Firmware). Requires ESP-IDF v5.4.x.

set -e
cd "$(dirname "$0")/firmware/esp-brookesia"

[ -z "$IDF_PATH" ] && { echo "Source ESP-IDF first: . \$IDF_PATH/export.sh"; exit 1; }

idf.py set-target esp32s3 2>/dev/null || true
idf.py build
