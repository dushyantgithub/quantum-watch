# Quantum Watch

Custom smartwatch firmware and iPhone companion for the **Waveshare ESP32-S3 Touch AMOLED 2.06"** board.

The project uses:
- ESP32-S3 firmware on LVGL 9 and esp-brookesia
- an iPhone companion app for BLE, HealthKit, location/weather, and voice-assistant bridging
- a custom BLE service plus ANCS support for iOS notifications

## Project Layout

```text
quantum-watch/
├── firmware/esp-brookesia/          # ESP-IDF firmware
├── ios/QuantumWatchCompanion/       # iPhone companion app
├── tools/                           # image/icon/helper scripts
├── deploy.sh                        # preferred build + flash flow
├── README.md
└── agent.md                         # project-specific engineering rules
```

## Current Features

- Voice Assistant app on the watch
- Health app on the watch
- Apple Health data sync from iPhone to watch
- Time, date, location, and weather sync from iPhone to watch
- ANCS notifications from iPhone
- Incoming call overlay and notification drawer
- Away screen / watch face UI
- custom lightweight watch app icons

## Firmware Build And Flash

Use `./deploy.sh` for flashing. Do not use ad hoc flash commands unless debugging something specific.

```bash
./deploy.sh
```

Manual build and monitor:

```bash
source ~/esp/esp-idf/export.sh
cd firmware/esp-brookesia
idf.py build
idf.py -p /dev/cu.usbmodem101 monitor
```

## iPhone Companion App

The iPhone app lives in:

```text
ios/QuantumWatchCompanion/
```

Open the Xcode project:

```text
ios/QuantumWatchCompanion.xcodeproj
```

The app is responsible for:
- BLE connection management
- Apple Health reads and sync to the watch
- location and weather fetches
- voice assistant audio/response relay
- call observation and notification forwarding

After iPhone-side changes, rebuild and reinstall from Xcode to test on-device.

## Display Stability Rules

This watch is sensitive to LVGL and buffer misuse. Breaking these rules can cause:
- screen tearing
- partial draws
- freezes
- watchdog resets
- memory pressure and crashes

Critical constraints:

```c
// BSP display init
buffer_size = BSP_LCD_H_RES * 100;  // do not increase
double_buffer = true;
buff_dma = false;
buff_spiram = false;

// LVGL port config
.task_priority = 5;
.task_affinity = 1;
.task_max_sleep_ms = 10;
.timer_period_ms = 5;
```

Never do the following:
- enable PSRAM or DMA display buffers for the main draw buffers
- allocate full-screen LVGL buffers in app code
- increase the display buffer beyond `410 * 100`
- raise `task_max_sleep_ms` to large values
- add heavy continuous animations for simple app status

Safe UI patterns:
- `lv_label`, `lv_line`, `lv_obj`, simple cards, small icons
- static-first screens with minimal redraws
- infrequent timers
- async data push with cached UI state

## Creating New Watch Apps Safely

When adding a new app under `firmware/esp-brookesia/main/`:

1. Create:
   - `your_app.cpp`
   - `your_app.h`
   - optional `your_app_icon.c`
2. Register it with `ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(...)`.
3. Keep the layout lightweight and mostly static.
4. Clear `LV_OBJ_FLAG_SCROLLABLE` on containers and buttons unless scrolling is intentional.
5. Avoid blocking work in UI callbacks.
6. Guard all async BLE/audio/timer UI updates against stale LVGL object pointers.
7. Reuse labels/widgets instead of rebuilding the whole screen on every refresh.
8. Prefer phone-pushed or cached data instead of fragile request-only flows.

Validation checklist for a new app:
- `idf.py build`
- `./deploy.sh`
- open/close the app repeatedly on the watch
- test connected and disconnected phone cases
- verify no tearing, no freezes, no stale async updates

More detailed project rules are in [`agent.md`](./agent.md).

## BLE Overview

Custom service UUID:

```text
0000AA00-0000-1000-8000-00805F9B34FB
```

Main characteristics:
- `AA01` audio: watch -> phone
- `AA02` text response: phone -> watch
- `AA03` context sync: phone -> watch
- `AA04` notification sync: phone -> watch

The project also supports ANCS alongside the custom GATT service so iOS notifications can be shown on the watch.

## Buttons

Custom firmware behavior:
- `BOOT` short press: toggle screen on/off
- `BOOT` long press: deep sleep / shutdown
- `PWR`: navigate to home screen

## Restore Factory Firmware

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem101 erase_flash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem101 write_flash 0x0 release/ESP32-S3-Touch-AMOLED-2.06-xiaozhi-251104.bin
```

## Links

- [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06)
- [Waveshare Demo](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06)
