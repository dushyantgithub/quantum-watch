# Quantum Watch

[ESP32-S3-Touch-AMOLED-2.06](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06) – official [Factory Firmware](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06#FactoryFirmWare) (esp-brookesia).

## Structure

```
quantum-watch/
├── firmware/esp-brookesia/   # ESP-IDF project (source)
├── release/                  # Pre-built .bin for restore
├── setup.sh
└── README.md
```

## Build & Flash

```bash
cd firmware/esp-brookesia
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
idf.py -p /dev/cu.usbmodem1101 monitor
```

**Power**: Hold PWR 6s to power off; press PWR to power on.  
**Download mode**: Hold BOOT while powering on.

## Restore Factory Firmware

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 erase_flash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 write_flash 0x0 release/ESP32-S3-Touch-AMOLED-2.06-xiaozhi-251104.bin
```

## Links

- [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06)
- [Waveshare Demo](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06)
