#!/bin/bash
# Quantum Watch — firmware build and flash
#
# This script:
#   1. Builds the ESP-IDF project in firmware/esp-brookesia (idf.py build)
#   2. Flashes the watch over USB serial (idf.py flash)
#
# Prerequisites:
#   - ESP-IDF v5.5.x (project targets IDF 5.5 + LVGL 9)
#   - USB cable to the Waveshare ESP32-S3 Touch AMOLED board
#
# Usage:
#   ./deploy.sh                      # uses ESPPORT, or default port below
#   ./deploy.sh /dev/cu.usbmodem101  # explicit port (recommended on macOS)
#   ESPPORT=/dev/cu.usbmodem101 ./deploy.sh
#
# After flash, serial monitor:
#   cd firmware/esp-brookesia && idf.py -p /dev/cu.usbmodem101 monitor

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT/firmware/esp-brookesia"

# Pick ESP-IDF if idf.py is not already on PATH
if ! command -v idf.py &>/dev/null; then
    for IDF in "${IDF_PATH:-}" "$HOME/esp/esp-idf" "$HOME/esp/esp-idf-v5.5"; do
        if [[ -n "$IDF" && -f "$IDF/export.sh" ]]; then
            # shellcheck source=/dev/null
            source "$IDF/export.sh"
            break
        fi
    done
fi

if ! command -v idf.py &>/dev/null; then
    echo "Error: idf.py not found. Install ESP-IDF and either:"
    echo "  . \\\$IDF_PATH/export.sh"
    echo "  or set IDF_PATH to your esp-idf checkout (e.g. ~/esp/esp-idf)."
    exit 1
fi

# Port: CLI arg > ESPPORT env > sensible macOS default (override if yours differs)
PORT="${1:-${ESPPORT:-/dev/cu.usbmodem101}}"

echo "=== Build (firmware/esp-brookesia) ==="
idf.py build

echo ""
echo "=== Flash to $PORT ==="
idf.py -p "$PORT" flash

echo ""
echo "Done. Logs: cd firmware/esp-brookesia && idf.py -p $PORT monitor"
