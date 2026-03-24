# Quantum Watch

A custom smartwatch built on the **Waveshare ESP32-S3 Touch AMOLED 2.06"** (SH8601 controller, 410x502, QSPI, RGB565).

## Project Structure

- `firmware/esp-brookesia/` — ESP-IDF 5.5.3 firmware (LVGL 9, esp_lvgl_port, esp-brookesia framework)
- `ios/QuantumWatchCompanion/` — iOS companion app (SwiftUI, CoreBluetooth, CoreLocation)
- `tools/` — Utility scripts (image conversion, etc.)

## Firmware Development Rules

### Display & LVGL — Preventing Screen Tearing

The display uses QSPI with SPI DMA. PSRAM-backed buffers require internal DMA bounce buffers, which are limited (~224KB). Violating these constraints causes crashes or screen tearing.

**Critical configuration (do NOT change):**

```c
// In BSP display init (managed_components/.../esp32_s3_touch_amoled_2_06.c)
buffer_size = BSP_LCD_H_RES * 100;  // 410 * 100 = 41,000 pixels — DO NOT increase
double_buffer = true;
buff_dma = false;      // DMA RAM is too small for our buffers
buff_spiram = false;   // PSRAM needs bounce buffers that exceed DMA RAM
// Uses MALLOC_CAP_DEFAULT which allocates from internal RAM
```

```c
// LVGL port config (main.cpp)
.task_priority = 5,       // High priority for smooth rendering
.task_affinity = 1,       // Pin to core 1 (core 0 runs WiFi/BLE)
.task_max_sleep_ms = 10,  // MUST be low — 500ms caused visible tearing
.timer_period_ms = 5,     // Fast timer tick
```

**What will break the display:**
- Setting `buff_spiram = true` or `buff_dma = true` — causes "Failed to allocate priv TX buffer" crash
- Increasing buffer_size beyond 410*100 — OOM or DMA bounce buffer overflow
- Setting `task_max_sleep_ms` above ~20 — causes visible tearing/lag
- Allocating full-screen LVGL buffers (410*502*2 = 411KB) — too large for DMA

**What is safe:**
- Using `lv_line`, `lv_label`, `lv_obj` primitives — lightweight, no extra buffers
- Using `lv_image` with small images — fine as long as decode buffers fit
- Creating LVGL timers for periodic UI updates (1s intervals for clocks, etc.)
- Using `lv_layer_top()` for overlay screens (away screen pattern)

### BLE GATT Protocol

Service UUID: `0x0000AA00-0000-1000-8000-00805F9B34FB`

| Characteristic | UUID   | Direction      | Format |
|----------------|--------|----------------|--------|
| Audio          | 0xAA01 | Watch → Phone  | NOTIFY: [2-byte seq][238-byte IMA-ADPCM], end marker: 0xFFFFFFFF |
| Text Response  | 0xAA02 | Phone → Watch  | WRITE: [1-byte flags: 0x00=more, 0x01=final][UTF-8 chunk] |
| Context Sync   | 0xAA03 | Phone → Watch  | WRITE: `unix_timestamp\|temp_celsius\|location_name` |
| Notification   | 0xAA04 | Phone → Watch  | WRITE: `CI`=call incoming, `CE`=call ended, `CM`=call missed |

### ANCS (Apple Notification Center Service)

The watch receives all iOS notifications (WhatsApp, iMessage, Mail, etc.) via ANCS — a BLE GATT service built into iOS. This runs alongside the existing custom GATT server on the same BLE connection.

**Architecture: GATTC + GATTS coexistence**
- **GATTS** (server, APP_ID=0): Existing audio/text/context/notification service (0xAA00)
- **GATTC** (client, APP_ID=1): ANCS client that subscribes to iOS notifications
- Both operate on the same BLE connection. When iPhone connects, the GATTS CONNECT handler calls `ancs_on_phone_connected()` which opens a GATTC virtual connection on the same link.

**Key files:**
- `ancs_client.cpp` / `ancs_client.h` — ANCS GATT client (service discovery, characteristic subscription, protocol parsing)
- `notifications.cpp` / `notifications.h` — Notification UI (call overlay, notification drawer, badge)
- `voice_assistant.cpp` — BLE stack init, security params, advertising with ANCS solicitation

**BLE Security (required by ANCS):**
- Auth: `ESP_LE_AUTH_REQ_SC_MITM_BOND` (Secure Connections + MITM + Bonding)
- IO Capability: `ESP_IO_CAP_NONE` (Just Works pairing)
- Address type: `BLE_ADDR_TYPE_PUBLIC` (public address for advertising, companion app compatibility)
- The watch advertises the ANCS service UUID as a solicited service in the scan response

**ANCS protocol flow:**
1. iPhone connects → watch subscribes to ANCS Notification Source + Data Source
2. iOS sends 8-byte event on Notification Source (EventID, CategoryID, UID)
3. Watch requests attributes (Title, Message, AppIdentifier) via Control Point
4. iOS responds on Data Source (possibly fragmented across BLE packets)
5. Watch buffers fragments (500ms timeout), parses response, calls `notifications_add()`
6. `EventIDNotificationRemoved` → `notifications_remove()`
7. `CategoryIDIncomingCall` → `notifications_call_incoming()` (beep + overlay)

**What NOT to change:**
- ANCS UUIDs are fixed by Apple (service: `7905F431-B5CE-4E99-A40F-4B1E122D00D0`)
- Security params must stay as-is — ANCS requires encrypted bonded connection
- Data Source buffering uses esp_timer with 500ms timeout for fragment reassembly
- Notification text capped at 64 (title) + 128 (message) chars to avoid large LVGL allocations
- Only specific apps are shown: Phone calls, WhatsApp (calls + messages), Gmail, Apple Mail, Calendar (Google + Apple)
- WhatsApp calls trigger the call overlay with beep (same as phone calls)
- All other app notifications are silently dropped in `parse_data_source()`

### Build & Flash

```bash
source ~/esp/esp-idf/export.sh
idf.py build
idf.py flash
idf.py monitor  # serial monitor (Ctrl+] to exit)
```

## iOS App Development Rules

- Xcode project file: `ios/QuantumWatchCompanion.xcodeproj/project.pbxproj`
- When adding new .swift files, they MUST be added to project.pbxproj (PBXFileReference, PBXBuildFile, group children, Sources build phase)
- ID convention in pbxproj: `AA00000X` for PBXBuildFile, `AA10000X` for PBXFileReference, `AA30000X` for PBXGroup
- BLE auto-connects in background using CoreBluetooth state restoration (identifier: `com.quantumwatch.companion.ble`)
- Background modes: `bluetooth-central`, `location`, `remote-notification`, `fetch`
