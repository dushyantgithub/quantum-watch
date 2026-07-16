/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 * SPDX-License-Identifier: MIT
 */

#include "vokrr_state.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include "watch_theme.h"

namespace vokrr {
namespace {

constexpr uint32_t CACHE_MAGIC = 0x56574F53;  // VWOS
constexpr uint16_t CACHE_VERSION = 1;

struct MetricSeed {
    const char *key;
    const char *label;
    const char *unit;
    HealthCategory category;
    uint32_t color;
};

constexpr MetricSeed METRIC_SEEDS[] = {
    {"hr", "HR", "BPM", HealthCategory::Vitals, WATCH_COLOR_ACCENT},
    {"temp", "TEMP", "C", HealthCategory::Vitals, WATCH_COLOR_ACCENT},
    {"hrv", "HRV", "MS", HealthCategory::Vitals, WATCH_COLOR_ACCENT},
    {"spo2", "SPO2", "%", HealthCategory::Vitals, WATCH_COLOR_ACCENT},
    {"temperature_deviation", "TEMPERATURE DEVIATION", "C", HealthCategory::Vitals, WATCH_COLOR_ACCENT},
    {"average_body_temperature", "AVG BODY TEMPERATURE", "C", HealthCategory::Vitals, WATCH_COLOR_ACCENT},
    {"motion", "MOTION", "", HealthCategory::Vitals, WATCH_COLOR_ACCENT},

    {"glucose", "GLUCOSE", "MG/DL", HealthCategory::Glucose, WATCH_COLOR_CHAMPAGNE},
    {"average_glucose", "AVERAGE GLUCOSE", "MG/DL", HealthCategory::Glucose, WATCH_COLOR_CHAMPAGNE},
    {"glucose_variability", "GLUCOSE VARIABILITY", "%", HealthCategory::Glucose, WATCH_COLOR_CHAMPAGNE},
    {"hba1c", "HBA1C", "%", HealthCategory::Glucose, WATCH_COLOR_CHAMPAGNE},
    {"time_in_target", "TIME IN TARGET", "%", HealthCategory::Glucose, WATCH_COLOR_CHAMPAGNE},
    {"metabolic_score", "METABOLIC SCORE", "/100", HealthCategory::Glucose, WATCH_COLOR_CHAMPAGNE},

    {"steps", "STEPS", "", HealthCategory::Activity, WATCH_COLOR_ACCENT_LIGHT},
    {"active_minutes", "ACTIVE MINUTES", "MIN", HealthCategory::Activity, WATCH_COLOR_ACCENT_LIGHT},
    {"movement_index", "MOVEMENT INDEX", "/100", HealthCategory::Activity, WATCH_COLOR_ACCENT_LIGHT},
    {"movements", "MOVEMENTS", "", HealthCategory::Activity, WATCH_COLOR_ACCENT_LIGHT},
    {"vo2_max", "VO2 MAX", "ML/KG/MIN", HealthCategory::Activity, WATCH_COLOR_ACCENT_LIGHT},

    {"sleep", "SLEEP", "HRS", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"sleep_score", "SLEEP SCORE", "/100", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"total_sleep", "TOTAL SLEEP", "HRS", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"time_in_bed", "TIME IN BED", "HRS", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"sleep_efficiency", "SLEEP EFFICIENCY", "%", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"rem_sleep", "REM SLEEP", "HRS", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"deep_sleep", "DEEP SLEEP", "HRS", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"light_sleep", "LIGHT SLEEP", "HRS", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"restorative_sleep", "RESTORATIVE SLEEP", "%", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"full_sleep_cycles", "FULL SLEEP CYCLES", "", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"tosses_and_turns", "TOSSES & TURNS", "", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"night_rhr", "NIGHT RHR", "BPM", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"sleep_rhr", "SLEEP RHR", "BPM", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"avg_sleep_hrv", "AVG SLEEP HRV", "MS", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"hr_drop", "HR DROP", "BPM", HealthCategory::Sleep, WATCH_COLOR_SLEEP},
    {"morning_alertness", "MORNING ALERTNESS", "MIN", HealthCategory::Sleep, WATCH_COLOR_SLEEP},

    {"recovery", "RECOVERY", "%", HealthCategory::Recovery, WATCH_COLOR_ACCENT},
    {"recovery_index", "RECOVERY INDEX", "/100", HealthCategory::Recovery, WATCH_COLOR_ACCENT},
};

static_assert(sizeof(METRIC_SEEDS) / sizeof(METRIC_SEEDS[0]) == MAX_HEALTH_METRICS);

struct CacheBlob {
    uint32_t magic;
    uint16_t version;
    uint8_t room_count;
    Room rooms[MAX_ROOMS];
    uint8_t metric_count;
    HealthMetric metrics[MAX_HEALTH_METRICS];
    uint32_t last_health_sync_epoch;
};

Snapshot s_state{};
SemaphoreHandle_t s_mutex = nullptr;

template <size_t N>
void copy_text(char (&destination)[N], const char *source)
{
    if (!source) {
        destination[0] = '\0';
        return;
    }
    std::snprintf(destination, N, "%s", source);
}

void seed_spark(HealthMetric &metric)
{
    std::fill(std::begin(metric.spark), std::end(metric.spark), static_cast<int16_t>(500));
}

void initialize_metrics(Snapshot &state)
{
    state.metric_count = MAX_HEALTH_METRICS;
    for (size_t index = 0; index < MAX_HEALTH_METRICS; ++index) {
        HealthMetric &metric = state.metrics[index];
        const MetricSeed &seed = METRIC_SEEDS[index];
        copy_text(metric.key, seed.key);
        copy_text(metric.label, seed.label);
        copy_text(metric.value, "--");
        copy_text(metric.unit, seed.unit);
        metric.category = seed.category;
        metric.color = seed.color;
        metric.available = false;
        seed_spark(metric);
    }
}

void add_device(Room &room, const char *id, const char *name, const char *type, uint16_t watts, bool on)
{
    if (room.device_count >= MAX_DEVICES_PER_ROOM) {
        return;
    }
    Device &device = room.devices[room.device_count++];
    copy_text(device.id, id);
    copy_text(device.name, name);
    copy_text(device.type, type);
    device.watts = watts;
    device.is_on = on;
    device.available = true;
    device.pending = false;
}

Room &add_room(Snapshot &state, const char *id, const char *name, const char *icon)
{
    Room &room = state.rooms[state.room_count++];
    copy_text(room.id, id);
    copy_text(room.name, name);
    copy_text(room.icon, icon);
    room.device_count = 0;
    return room;
}

void initialize_fallback_rooms(Snapshot &state)
{
    state.room_count = 0;
    Room &living = add_room(state, "living", "Living Room", "living");
    add_device(living, "lv_bulb", "Bulb", "light", 9, false);
    add_device(living, "lv_fan", "Fan", "fan", 45, false);
    add_device(living, "lv_socket", "Socket", "switch", 30, false);
    add_device(living, "lv_tube", "Tubelight", "light", 18, false);

    Room &kitchen = add_room(state, "kitchen", "Kitchen", "kitchen");
    add_device(kitchen, "kt_bulb_l", "Left Bulb", "light", 9, false);
    add_device(kitchen, "kt_bulb_r", "Right Bulb", "light", 9, false);

    Room &gaming = add_room(state, "gaming", "Gaming Room", "gaming");
    add_device(gaming, "gm_tube1", "Tubelight 1", "light", 18, false);
    add_device(gaming, "gm_socket", "Socket", "switch", 30, false);
    add_device(gaming, "gm_fan", "Fan", "fan", 45, false);
    add_device(gaming, "gm_bulb", "Bulb", "light", 9, false);
    add_device(gaming, "gm_tube2", "Tubelight 2", "light", 18, false);

    Room &bedroom = add_room(state, "bedroom", "Bedroom", "bedroom");
    add_device(bedroom, "bd_tube1", "Tubelight 1", "light", 18, false);
    add_device(bedroom, "bd_aircon", "Aircon Socket", "switch", 1200, false);
    add_device(bedroom, "bd_bulb", "Bulb", "light", 9, false);
    add_device(bedroom, "bd_fan", "Fan", "fan", 45, false);
    add_device(bedroom, "bd_socket", "Socket", "switch", 30, false);
    add_device(bedroom, "bd_tube2", "Tubelight 2", "light", 18, false);

    Room &bathroom = add_room(state, "bathroom", "Bathroom", "bathroom");
    add_device(bathroom, "bt_geyser", "Geyser", "switch", 2000, false);

    Room &dining = add_room(state, "dining", "Dining Room", "dining");
    add_device(dining, "dn_tube", "Tubelight", "light", 18, false);
    add_device(dining, "dn_bulb", "Bulb", "light", 9, false);
}

bool lock(TickType_t wait = portMAX_DELAY)
{
    return s_mutex && xSemaphoreTake(s_mutex, wait) == pdTRUE;
}

void unlock()
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

void changed()
{
    ++s_state.revision;
    if (s_state.revision == 0) {
        s_state.revision = 1;
    }
}

}  // namespace

void state_init()
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (!lock()) {
        return;
    }
    std::memset(&s_state, 0, sizeof(s_state));
    s_state.revision = 1;
    s_state.battery_percent = -1;
    s_state.battery_health_percent = 96;
    initialize_metrics(s_state);
    initialize_fallback_rooms(s_state);
    unlock();
    state_load_cache();
}

bool state_copy(Snapshot &out)
{
    if (!lock(pdMS_TO_TICKS(25))) {
        return false;
    }
    std::memcpy(&out, &s_state, sizeof(out));
    unlock();
    return true;
}

void state_set_wifi(bool connected, const char *ssid, int rssi, const char *ip)
{
    if (!lock()) return;
    s_state.wifi_connected = connected;
    s_state.wifi_rssi = static_cast<int16_t>(rssi);
    copy_text(s_state.wifi_ssid, ssid);
    copy_text(s_state.ip_address, ip);
    changed();
    unlock();
}

void state_set_ble(bool connected, const char *device_name)
{
    if (!lock()) return;
    s_state.ble_connected = connected;
    if (device_name) copy_text(s_state.paired_device, device_name);
    if (connected) s_state.last_ios_sync_epoch = static_cast<uint32_t>(std::time(nullptr));
    changed();
    unlock();
}

void state_set_battery(int percent, bool charging)
{
    if (!lock()) return;
    s_state.battery_percent = static_cast<int8_t>(std::clamp(percent, -1, 100));
    s_state.battery_charging = charging;
    if (percent >= 0) {
        s_state.battery_estimate_minutes = static_cast<uint16_t>(percent * 13);
    }
    changed();
    unlock();
}

void state_replace_rooms(const Room *rooms, size_t count)
{
    if (!rooms || !lock()) return;
    const size_t bounded = std::min(count, MAX_ROOMS);
    std::memset(s_state.rooms, 0, sizeof(s_state.rooms));
    std::memcpy(s_state.rooms, rooms, bounded * sizeof(Room));
    s_state.room_count = static_cast<uint8_t>(bounded);
    changed();
    unlock();
}

bool state_set_device(const char *device_id, bool on, bool pending, bool *previous)
{
    if (!device_id || !lock()) return false;
    for (size_t room_index = 0; room_index < s_state.room_count; ++room_index) {
        Room &room = s_state.rooms[room_index];
        for (size_t device_index = 0; device_index < room.device_count; ++device_index) {
            Device &device = room.devices[device_index];
            if (std::strcmp(device.id, device_id) == 0) {
                if (previous) *previous = device.is_on;
                device.is_on = on;
                device.pending = pending;
                changed();
                unlock();
                return true;
            }
        }
    }
    unlock();
    return false;
}

bool state_get_device(const char *device_id, bool *on, bool *pending)
{
    if (!device_id || !on || !lock()) return false;
    for (size_t room_index = 0; room_index < s_state.room_count; ++room_index) {
        const Room &room = s_state.rooms[room_index];
        for (size_t device_index = 0; device_index < room.device_count; ++device_index) {
            const Device &device = room.devices[device_index];
            if (std::strcmp(device.id, device_id) == 0) {
                *on = device.is_on;
                if (pending) *pending = device.pending;
                unlock();
                return true;
            }
        }
    }
    unlock();
    return false;
}

void state_replace_health(const HealthMetric *metrics, size_t count, uint32_t sync_epoch)
{
    if (!metrics || !lock()) return;
    const size_t bounded = std::min(count, MAX_HEALTH_METRICS);
    std::memcpy(s_state.metrics, metrics, bounded * sizeof(HealthMetric));
    s_state.metric_count = static_cast<uint8_t>(bounded);
    s_state.last_health_sync_epoch = sync_epoch;
    changed();
    unlock();
}

int state_metric_index(const Snapshot &snapshot, const char *key)
{
    if (!key) return -1;
    for (size_t index = 0; index < snapshot.metric_count; ++index) {
        if (std::strcmp(snapshot.metrics[index].key, key) == 0) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void state_apply_legacy_health(const char *payload)
{
    if (!payload) return;
    std::unique_ptr<Snapshot> snapshot_storage(new (std::nothrow) Snapshot{});
    if (!snapshot_storage || !state_copy(*snapshot_storage)) return;
    Snapshot &snapshot = *snapshot_storage;

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", payload);
    char *save = nullptr;
    char *steps = strtok_r(buffer, "|", &save);
    (void)strtok_r(nullptr, "|", &save);  // active energy
    (void)strtok_r(nullptr, "|", &save);  // distance
    char *heart = strtok_r(nullptr, "|", &save);

    const int steps_index = state_metric_index(snapshot, "steps");
    if (steps_index >= 0 && steps) {
        copy_text(snapshot.metrics[steps_index].value, steps);
        snapshot.metrics[steps_index].available = true;
    }
    const int heart_index = state_metric_index(snapshot, "hr");
    if (heart_index >= 0 && heart) {
        char *space = std::strchr(heart, ' ');
        if (space) *space = '\0';
        copy_text(snapshot.metrics[heart_index].value, heart);
        snapshot.metrics[heart_index].available = std::strcmp(heart, "--") != 0;
    }
    state_replace_health(snapshot.metrics, snapshot.metric_count, static_cast<uint32_t>(std::time(nullptr)));
}

void state_load_cache()
{
    nvs_handle_t handle = 0;
    if (nvs_open("vokrr", NVS_READONLY, &handle) != ESP_OK) return;
    std::unique_ptr<CacheBlob> cache_storage(new (std::nothrow) CacheBlob{});
    if (!cache_storage) {
        nvs_close(handle);
        return;
    }
    CacheBlob &cache = *cache_storage;
    size_t size = sizeof(cache);
    const esp_err_t result = nvs_get_blob(handle, "snapshot", &cache, &size);
    nvs_close(handle);
    if (result != ESP_OK || size != sizeof(cache) || cache.magic != CACHE_MAGIC || cache.version != CACHE_VERSION) {
        return;
    }
    if (!lock()) return;
    s_state.room_count = std::min<uint8_t>(cache.room_count, MAX_ROOMS);
    std::memcpy(s_state.rooms, cache.rooms, sizeof(s_state.rooms));
    s_state.metric_count = std::min<uint8_t>(cache.metric_count, MAX_HEALTH_METRICS);
    std::memcpy(s_state.metrics, cache.metrics, sizeof(s_state.metrics));
    s_state.last_health_sync_epoch = cache.last_health_sync_epoch;
    changed();
    unlock();
}

void state_save_cache()
{
    std::unique_ptr<CacheBlob> cache_storage(new (std::nothrow) CacheBlob{});
    if (!cache_storage) return;
    CacheBlob &cache = *cache_storage;
    cache.magic = CACHE_MAGIC;
    cache.version = CACHE_VERSION;
    if (!lock(pdMS_TO_TICKS(100))) return;
    cache.room_count = s_state.room_count;
    std::memcpy(cache.rooms, s_state.rooms, sizeof(cache.rooms));
    cache.metric_count = s_state.metric_count;
    std::memcpy(cache.metrics, s_state.metrics, sizeof(cache.metrics));
    cache.last_health_sync_epoch = s_state.last_health_sync_epoch;
    unlock();

    nvs_handle_t handle = 0;
    if (nvs_open("vokrr", NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_blob(handle, "snapshot", &cache, sizeof(cache)) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

}  // namespace vokrr
