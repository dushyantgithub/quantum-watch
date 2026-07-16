/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace vokrr {

constexpr size_t MAX_ROOMS = 6;
constexpr size_t MAX_DEVICES_PER_ROOM = 8;
constexpr size_t MAX_HEALTH_METRICS = 36;
constexpr size_t SPARK_POINTS = 22;

enum class HealthCategory : uint8_t {
    Vitals,
    Glucose,
    Activity,
    Sleep,
    Recovery,
};

struct Device {
    char id[64];
    char name[40];
    char type[16];
    uint16_t watts;
    bool is_on;
    bool available;
    bool pending;
};

struct Room {
    char id[40];
    char name[40];
    char icon[20];
    uint8_t device_count;
    Device devices[MAX_DEVICES_PER_ROOM];
};

struct HealthMetric {
    char key[32];
    char label[40];
    char value[20];
    char unit[16];
    HealthCategory category;
    uint32_t color;
    int16_t spark[SPARK_POINTS];
    bool available;
};

struct Snapshot {
    uint32_t revision;
    uint8_t room_count;
    Room rooms[MAX_ROOMS];
    uint8_t metric_count;
    HealthMetric metrics[MAX_HEALTH_METRICS];

    bool wifi_connected;
    int16_t wifi_rssi;
    char wifi_ssid[33];
    char ip_address[16];
    bool ble_connected;
    char paired_device[40];
    uint32_t last_ios_sync_epoch;
    uint32_t last_health_sync_epoch;

    int8_t battery_percent;
    bool battery_charging;
    uint8_t battery_health_percent;
    uint16_t battery_estimate_minutes;
};

void state_init();
bool state_copy(Snapshot &out);

void state_set_wifi(bool connected, const char *ssid, int rssi, const char *ip);
void state_set_ble(bool connected, const char *device_name = nullptr);
void state_set_battery(int percent, bool charging);

void state_replace_rooms(const Room *rooms, size_t count);
bool state_set_device(const char *device_id, bool on, bool pending, bool *previous = nullptr);
bool state_get_device(const char *device_id, bool *on, bool *pending = nullptr);

void state_replace_health(const HealthMetric *metrics, size_t count, uint32_t sync_epoch);
void state_apply_legacy_health(const char *payload);
int state_metric_index(const Snapshot &snapshot, const char *key);

void state_load_cache();
void state_save_cache();

}  // namespace vokrr
