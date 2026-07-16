/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 * SPDX-License-Identifier: MIT
 */

#include "vokrr_network.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <string>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "vokrr_config.h"
#include "vokrr_state.h"

namespace vokrr {
namespace {

constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
// One day of normalized Ultrahuman samples is currently ~250 KB; leave room for denser days.
constexpr size_t MAX_HTTP_RESPONSE = 384 * 1024;
constexpr size_t ACCESS_TOKEN_CAPACITY = 2048;
constexpr size_t TOGGLE_QUEUE_DEPTH = 8;
constexpr std::time_t MIN_VALID_UNIX_TIME = 1704067200;  // 2024-01-01 UTC

const char *TAG = "VokrrNet";

EventGroupHandle_t s_wifi_events = nullptr;
QueueHandle_t s_toggle_queue = nullptr;
TaskHandle_t s_network_task = nullptr;
char s_access_token[ACCESS_TOKEN_CAPACITY] = {};
uint8_t s_wifi_profile = 0;
uint8_t s_retry_count = 0;
volatile bool s_force_refresh = false;

struct ToggleRequest {
    char device_id[64];
    bool desired_state;
    bool previous_state;
};

struct HttpResponse {
    std::string body;
    bool truncated = false;
    int status = 0;
};

template <size_t N>
void copy_text(char (&destination)[N], const char *source)
{
    std::snprintf(destination, N, "%s", source ? source : "");
}

template <size_t N>
void copy_metric_unit(char (&destination)[N], const char *source)
{
    size_t output = 0;
    for (size_t input = 0; source && source[input] != '\0' && output + 1 < N;) {
        const auto first = static_cast<unsigned char>(source[input]);
        const auto second = static_cast<unsigned char>(source[input + 1]);
        const auto third = second != 0 ? static_cast<unsigned char>(source[input + 2]) : 0;
        if (first == 0xC2 && second == 0xB0) {  // UTF-8 degree sign
            input += 2;
            continue;
        }
        if (first == 0xE2 && second == 0x84 && third == 0x83) {  // UTF-8 Celsius sign
            destination[output++] = 'C';
            input += 3;
            continue;
        }
        destination[output++] = source[input++];
    }
    destination[output] = '\0';
}

const char *profile_ssid(uint8_t profile)
{
    return profile == 0 ? VOKRR_PRIMARY_SSID : VOKRR_SECONDARY_SSID;
}

const char *profile_password(uint8_t profile)
{
    return profile == 0 ? VOKRR_PRIMARY_PASSWORD : VOKRR_SECONDARY_PASSWORD;
}

void configure_wifi_profile(uint8_t profile)
{
    wifi_config_t config{};
    std::snprintf(reinterpret_cast<char *>(config.sta.ssid), sizeof(config.sta.ssid), "%s", profile_ssid(profile));
    std::snprintf(reinterpret_cast<char *>(config.sta.password), sizeof(config.sta.password), "%s", profile_password(profile));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &config));
}

void start_sntp()
{
    static bool initialized = false;
    if (initialized) return;
    setenv("TZ", VOKRR_TIMEZONE, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, const_cast<char *>("time.cloudflare.com"));
    esp_sntp_setservername(1, const_cast<char *>("pool.ntp.org"));
    esp_sntp_init();
    initialized = true;
}

bool wait_for_valid_clock()
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        const std::time_t now = std::time(nullptr);
        if (now >= MIN_VALID_UNIX_TIME) {
            ESP_LOGI(TAG, "Clock synchronized (unix %lld)", static_cast<long long>(now));
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGW(TAG, "Waiting for network time before authenticated HTTPS requests");
    return false;
}

void update_connected_state(const ip_event_got_ip_t *event)
{
    wifi_ap_record_t access_point{};
    const int rssi = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK ? access_point.rssi : 0;
    char ip[16] = {};
    if (event) {
        std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
    }
    state_set_wifi(true, profile_ssid(s_wifi_profile), rssi, ip);
    ESP_LOGI(TAG, "WiFi connected (profile %u, RSSI %d, IP %s)",
             static_cast<unsigned>(s_wifi_profile + 1), rssi, ip);
}

void wifi_event_handler(void *, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        state_set_wifi(false, profile_ssid(s_wifi_profile), 0, "");
        ++s_retry_count;
        if (s_retry_count >= 4 && VOKRR_SECONDARY_SSID[0] != '\0') {
            s_retry_count = 0;
            s_wifi_profile = (s_wifi_profile + 1) % 2;
            configure_wifi_profile(s_wifi_profile);
        }
        esp_wifi_connect();
        return;
    }
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        update_connected_state(static_cast<const ip_event_got_ip_t *>(event_data));
        start_sntp();
    }
}

esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    auto *response = static_cast<HttpResponse *>(event->user_data);
    if (!response || event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    if (response->body.size() + static_cast<size_t>(event->data_len) > MAX_HTTP_RESPONSE) {
        response->truncated = true;
        return ESP_OK;
    }
    response->body.append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
    return ESP_OK;
}

std::string make_url(const char *path)
{
    std::string base = VOKRR_SERVER_URL;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (path && path[0] != '/') base.push_back('/');
    base += path ? path : "";
    return base;
}

bool http_request(esp_http_client_method_t method, const char *path, const char *body, bool authenticated, HttpResponse &response)
{
    if (VOKRR_SERVER_URL[0] == '\0') return false;
    const std::string url = make_url(path);
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.method = method;
    config.event_handler = http_event_handler;
    config.user_data = &response;
    config.timeout_ms = 15000;
    config.buffer_size = 2048;
    config.buffer_size_tx = 2048;
    config.keep_alive_enable = true;
    // Cloudflare's public API route rejects the ESP-IDF client's default user agent.
    config.user_agent = "VokrrWatch/1.0";
    if (url.rfind("https://", 0) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_header(client, "Accept", "application/json");
    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, std::strlen(body));
    }
    if (authenticated && s_access_token[0] != '\0') {
        std::string header = "Bearer ";
        header += s_access_token;
        esp_http_client_set_header(client, "Authorization", header.c_str());
    }

    const esp_err_t result = esp_http_client_perform(client);
    response.status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return result == ESP_OK && !response.truncated;
}

bool login()
{
    if (VOKRR_SERVER_USERNAME[0] == '\0' || VOKRR_SERVER_PASSWORD[0] == '\0') return false;
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "username", VOKRR_SERVER_USERNAME);
    cJSON_AddStringToObject(payload, "password", VOKRR_SERVER_PASSWORD);
    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!body) return false;

    HttpResponse response;
    const bool performed = http_request(HTTP_METHOD_POST, "/api/auth/login", body, false, response);
    cJSON_free(body);
    if (!performed || response.status != 200) return false;

    cJSON *root = cJSON_ParseWithLength(response.body.data(), response.body.size());
    if (!root) return false;
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "access_token");
    const bool ok = cJSON_IsString(token) && token->valuestring && std::strlen(token->valuestring) < sizeof(s_access_token);
    if (ok) copy_text(s_access_token, token->valuestring);
    cJSON_Delete(root);
    return ok;
}

bool ensure_login()
{
    return s_access_token[0] != '\0' || login();
}

const char *json_string(const cJSON *object, const char *key, const char *fallback = "")
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

double json_number(const cJSON *object, const char *key, double fallback = 0.0)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

bool json_container_span(const std::string &json, const char *key, size_t search_begin, size_t search_end,
                         char open, char close, size_t &value_begin, size_t &value_end)
{
    std::string token = "\"";
    token += key;
    token += "\"";
    size_t key_pos = json.find(token, search_begin);
    while (key_pos != std::string::npos && key_pos < search_end) {
        size_t cursor = key_pos + token.size();
        while (cursor < search_end && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
        if (cursor >= search_end || json[cursor] != ':') {
            key_pos = json.find(token, cursor);
            continue;
        }
        ++cursor;
        while (cursor < search_end && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
        if (cursor >= search_end || json[cursor] != open) {
            key_pos = json.find(token, cursor);
            continue;
        }

        bool in_string = false;
        bool escaped = false;
        int depth = 0;
        for (size_t index = cursor; index < search_end; ++index) {
            const char character = json[index];
            if (in_string) {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') in_string = false;
                continue;
            }
            if (character == '"') {
                in_string = true;
            } else if (character == open) {
                ++depth;
            } else if (character == close && --depth == 0) {
                value_begin = cursor;
                value_end = index + 1;
                return true;
            }
        }
        return false;
    }
    return false;
}

bool replace_json_container(std::string &json, const char *key, char open, char close, const char *replacement)
{
    size_t begin = 0;
    size_t end = 0;
    if (!json_container_span(json, key, 0, json.size(), open, close, begin, end)) return false;
    json.replace(begin, end - begin, replacement);
    return true;
}

size_t read_series_values(const std::string &current_json, const char *series_name, double *values, size_t capacity)
{
    size_t series_begin = 0;
    size_t series_end = 0;
    if (!json_container_span(current_json, "series", 0, current_json.size(), '{', '}', series_begin, series_end)) {
        return 0;
    }
    size_t array_begin = 0;
    size_t array_end = 0;
    if (!json_container_span(current_json, series_name, series_begin, series_end, '[', ']', array_begin, array_end)) {
        return 0;
    }

    size_t count = 0;
    size_t cursor = array_begin;
    while (count < capacity) {
        cursor = current_json.find("\"value\"", cursor);
        if (cursor == std::string::npos || cursor >= array_end) break;
        cursor = current_json.find(':', cursor + 7);
        if (cursor == std::string::npos || cursor >= array_end) break;
        ++cursor;
        char *number_end = nullptr;
        const double value = std::strtod(current_json.c_str() + cursor, &number_end);
        if (number_end != current_json.c_str() + cursor && std::isfinite(value)) {
            values[count++] = value;
            cursor = static_cast<size_t>(number_end - current_json.c_str());
        }
    }
    return count;
}

bool parse_rooms(const std::string &body)
{
    cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }

    std::unique_ptr<Room[]> rooms_storage(new (std::nothrow) Room[MAX_ROOMS]{});
    if (!rooms_storage) {
        cJSON_Delete(root);
        return false;
    }
    Room *rooms = rooms_storage.get();
    size_t room_count = 0;
    cJSON *room_json = nullptr;
    cJSON_ArrayForEach(room_json, root) {
        if (room_count >= MAX_ROOMS || !cJSON_IsObject(room_json)) break;
        Room &room = rooms[room_count++];
        copy_text(room.id, json_string(room_json, "id"));
        copy_text(room.name, json_string(room_json, "name", "Room"));
        copy_text(room.icon, json_string(room_json, "icon", "room"));

        const cJSON *devices = cJSON_GetObjectItemCaseSensitive(room_json, "devices");
        if (!cJSON_IsArray(devices)) continue;
        cJSON *device_json = nullptr;
        cJSON_ArrayForEach(device_json, devices) {
            if (room.device_count >= MAX_DEVICES_PER_ROOM || !cJSON_IsObject(device_json)) break;
            Device &device = room.devices[room.device_count++];
            copy_text(device.id, json_string(device_json, "id"));
            copy_text(device.name, json_string(device_json, "name", "Device"));
            copy_text(device.type, json_string(device_json, "type", "unknown"));
            const cJSON *state = cJSON_GetObjectItemCaseSensitive(device_json, "state");
            const cJSON *is_on = cJSON_GetObjectItemCaseSensitive(state, "is_on");
            device.is_on = cJSON_IsTrue(is_on);
            const char *state_text = json_string(state, "state", "unknown");
            device.available = std::strcmp(state_text, "unavailable") != 0;
            device.pending = false;
            const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(state, "attributes");
            device.watts = static_cast<uint16_t>(std::clamp(json_number(attributes, "power", 0), 0.0, 65535.0));
        }
    }
    cJSON_Delete(root);
    if (room_count == 0) return false;
    state_replace_rooms(rooms, room_count);
    ESP_LOGI(TAG, "Rooms synchronized (%u rooms, %u bytes)",
             static_cast<unsigned>(room_count), static_cast<unsigned>(body.size()));
    return true;
}

int metric_index(HealthMetric *metrics, size_t count, const char *key)
{
    for (size_t index = 0; index < count; ++index) {
        if (std::strcmp(metrics[index].key, key) == 0) return static_cast<int>(index);
    }
    return -1;
}

void flat_spark(HealthMetric &metric)
{
    std::fill(std::begin(metric.spark), std::end(metric.spark), static_cast<int16_t>(500));
}

void set_metric_text(HealthMetric *metrics, size_t count, const char *key, const char *value, const char *unit = nullptr)
{
    const int index = metric_index(metrics, count, key);
    if (index < 0 || !value) return;
    copy_text(metrics[index].value, value);
    if (unit) copy_metric_unit(metrics[index].unit, unit);
    metrics[index].available = true;
    flat_spark(metrics[index]);
}

void set_metric_number(HealthMetric *metrics, size_t count, const char *key, double value, int precision = 0, const char *unit = nullptr)
{
    if (!std::isfinite(value)) return;
    char formatted[24];
    if (precision == 0) {
        std::snprintf(formatted, sizeof(formatted), "%.0f", value);
    } else {
        std::snprintf(formatted, sizeof(formatted), "%.*f", precision, value);
    }
    set_metric_text(metrics, count, key, formatted, unit);
}

const cJSON *path(const cJSON *root, const char *a, const char *b = nullptr, const char *c = nullptr)
{
    const cJSON *value = root;
    for (const char *part : {a, b, c}) {
        if (!part) break;
        value = cJSON_GetObjectItemCaseSensitive(value, part);
        if (!value) break;
    }
    return value;
}

void set_from_metric_object(HealthMetric *metrics, size_t count, const char *key, const cJSON *object, int precision = 0, const char *unit = nullptr)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, "value");
    if (!cJSON_IsNumber(value)) return;
    const char *source_unit = unit;
    if (!source_unit) source_unit = json_string(object, "unit", nullptr);
    set_metric_number(metrics, count, key, value->valuedouble, precision, source_unit);
}

void set_duration(HealthMetric *metrics, size_t count, const char *key, const cJSON *seconds_json)
{
    if (!cJSON_IsNumber(seconds_json)) return;
    const int total_minutes = std::max(0, seconds_json->valueint / 60);
    char formatted[16];
    std::snprintf(formatted, sizeof(formatted), "%d:%02d", total_minutes / 60, total_minutes % 60);
    set_metric_text(metrics, count, key, formatted, "HRS");
}

void apply_series_values(HealthMetric *metrics, size_t count, const char *metric_key,
                         const double *values, size_t value_count, double *average_out = nullptr,
                         double *variation_out = nullptr, double *target_out = nullptr)
{
    if (!values || value_count == 0) return;
    double sum = 0;
    double min_value = values[0];
    double max_value = values[0];
    size_t target_count = 0;
    for (size_t index = 0; index < value_count; ++index) {
        sum += values[index];
        min_value = std::min(min_value, values[index]);
        max_value = std::max(max_value, values[index]);
        if (values[index] >= 70 && values[index] <= 140) ++target_count;
    }
    const double average = sum / value_count;
    double variance = 0;
    for (size_t index = 0; index < value_count; ++index) {
        const double delta = values[index] - average;
        variance += delta * delta;
    }
    variance /= value_count;
    if (average_out) *average_out = average;
    if (variation_out) *variation_out = average == 0 ? 0 : std::sqrt(variance) / std::abs(average) * 100.0;
    if (target_out) *target_out = static_cast<double>(target_count) / value_count * 100.0;

    const int index = metric_index(metrics, count, metric_key);
    if (index < 0) return;
    HealthMetric &metric = metrics[index];
    const double span = std::max(0.001, max_value - min_value);
    for (size_t point = 0; point < SPARK_POINTS; ++point) {
        const size_t source = point * (value_count - 1) / (SPARK_POINTS - 1);
        metric.spark[point] = static_cast<int16_t>(120 + ((values[source] - min_value) / span) * 760);
    }
}

double stage_seconds(const cJSON *stages, const char *name)
{
    if (!cJSON_IsArray(stages)) return -1;
    cJSON *stage = nullptr;
    cJSON_ArrayForEach(stage, stages) {
        if (std::strcmp(json_string(stage, "stage"), name) == 0) {
            return json_number(stage, "seconds", -1);
        }
    }
    return -1;
}

bool parse_health(const std::string &body)
{
    size_t current_begin = 0;
    size_t current_end = 0;
    if (!json_container_span(body, "current", 0, body.size(), '{', '}', current_begin, current_end)) return false;

    const std::string current_json = body.substr(current_begin, current_end - current_begin);
    constexpr size_t SERIES_CAPACITY = 128;
    std::unique_ptr<double[]> series_storage(new (std::nothrow) double[SERIES_CAPACITY * 5]{});
    if (!series_storage) return false;
    double *hr_values = series_storage.get();
    double *hrv_values = hr_values + SERIES_CAPACITY;
    double *temp_values = hrv_values + SERIES_CAPACITY;
    double *motion_values = temp_values + SERIES_CAPACITY;
    double *glucose_values = motion_values + SERIES_CAPACITY;
    const size_t hr_value_count = read_series_values(current_json, "hr", hr_values, SERIES_CAPACITY);
    const size_t hrv_value_count = read_series_values(current_json, "hrv", hrv_values, SERIES_CAPACITY);
    const size_t temp_value_count = read_series_values(current_json, "temp", temp_values, SERIES_CAPACITY);
    const size_t motion_value_count = read_series_values(current_json, "motion", motion_values, SERIES_CAPACITY);
    const size_t glucose_value_count = read_series_values(current_json, "glucose", glucose_values, SERIES_CAPACITY);

    std::string summary_json = current_json;
    replace_json_container(summary_json, "series", '{', '}', "{}");
    replace_json_container(summary_json, "timeline", '[', ']', "[]");
    cJSON *root = cJSON_ParseWithLength(summary_json.data(), summary_json.size());
    const cJSON *current = root;
    if (!cJSON_IsObject(current)) {
        cJSON_Delete(root);
        return false;
    }

    std::unique_ptr<Snapshot> snapshot_storage(new (std::nothrow) Snapshot{});
    std::unique_ptr<HealthMetric[]> metrics_storage(new (std::nothrow) HealthMetric[MAX_HEALTH_METRICS]{});
    if (!snapshot_storage || !metrics_storage || !state_copy(*snapshot_storage)) {
        cJSON_Delete(root);
        return false;
    }
    Snapshot &snapshot = *snapshot_storage;
    HealthMetric *metrics = metrics_storage.get();
    std::memcpy(metrics, snapshot.metrics, sizeof(snapshot.metrics));
    for (size_t index = 0; index < snapshot.metric_count; ++index) {
        copy_text(metrics[index].value, "--");
        metrics[index].available = false;
        flat_spark(metrics[index]);
    }

    set_from_metric_object(metrics, snapshot.metric_count, "hr", path(current, "latest_heart_rate"));
    set_from_metric_object(metrics, snapshot.metric_count, "temp", path(current, "skin_temperature"), 1);
    set_from_metric_object(metrics, snapshot.metric_count, "average_body_temperature", path(current, "skin_temperature"), 1);
    set_from_metric_object(metrics, snapshot.metric_count, "hrv", path(current, "average_hrv"));
    set_from_metric_object(metrics, snapshot.metric_count, "spo2", path(current, "spo2"));
    set_from_metric_object(metrics, snapshot.metric_count, "vo2_max", path(current, "vo2_max"));
    set_from_metric_object(metrics, snapshot.metric_count, "temperature_deviation", path(current, "recovery", "temperature_deviation"), 1);

    set_from_metric_object(metrics, snapshot.metric_count, "steps", path(current, "activity", "steps"));
    set_from_metric_object(metrics, snapshot.metric_count, "active_minutes", path(current, "activity", "active_minutes"));
    set_from_metric_object(metrics, snapshot.metric_count, "movement_index", path(current, "activity", "movement_index"));

    set_from_metric_object(metrics, snapshot.metric_count, "sleep_score", path(current, "sleep", "score"));
    set_duration(metrics, snapshot.metric_count, "sleep", path(current, "sleep", "total_sleep_seconds"));
    set_duration(metrics, snapshot.metric_count, "total_sleep", path(current, "sleep", "total_sleep_seconds"));
    set_duration(metrics, snapshot.metric_count, "time_in_bed", path(current, "sleep", "time_in_bed_seconds"));
    set_from_metric_object(metrics, snapshot.metric_count, "sleep_efficiency", path(current, "sleep", "efficiency"));
    set_from_metric_object(metrics, snapshot.metric_count, "restorative_sleep", path(current, "sleep", "restorative_sleep"));

    const cJSON *stages = path(current, "sleep", "stages");
    for (const auto &stage : {std::pair<const char *, const char *>("rem", "rem_sleep"),
                              std::pair<const char *, const char *>("deep", "deep_sleep"),
                              std::pair<const char *, const char *>("light", "light_sleep")}) {
        const double seconds = stage_seconds(stages, stage.first);
        if (seconds >= 0) {
            cJSON temporary{};
            temporary.type = cJSON_Number;
            temporary.valuedouble = seconds;
            temporary.valueint = static_cast<int>(seconds);
            set_duration(metrics, snapshot.metric_count, stage.second, &temporary);
        }
    }

    const cJSON *cycles = path(current, "sleep", "full_sleep_cycles");
    if (cJSON_IsNumber(cycles)) set_metric_number(metrics, snapshot.metric_count, "full_sleep_cycles", cycles->valuedouble);
    const cJSON *tosses = path(current, "sleep", "tosses_and_turns");
    if (cJSON_IsNumber(tosses)) set_metric_number(metrics, snapshot.metric_count, "tosses_and_turns", tosses->valuedouble);
    const cJSON *alertness = path(current, "sleep", "morning_alertness_minutes");
    if (cJSON_IsNumber(alertness)) set_metric_number(metrics, snapshot.metric_count, "morning_alertness", alertness->valuedouble);

    set_from_metric_object(metrics, snapshot.metric_count, "night_rhr", path(current, "resting_heart_rate"));
    set_from_metric_object(metrics, snapshot.metric_count, "sleep_rhr", path(current, "recovery", "sleep_resting_hr"));
    set_from_metric_object(metrics, snapshot.metric_count, "avg_sleep_hrv", path(current, "recovery", "average_sleep_hrv"));
    set_from_metric_object(metrics, snapshot.metric_count, "hr_drop", path(current, "recovery", "heart_rate_drop"));
    set_from_metric_object(metrics, snapshot.metric_count, "recovery", path(current, "recovery", "score"), 0, "%");
    set_from_metric_object(metrics, snapshot.metric_count, "recovery_index", path(current, "recovery", "score"), 0, "/100");

    apply_series_values(metrics, snapshot.metric_count, "hr", hr_values, hr_value_count);
    apply_series_values(metrics, snapshot.metric_count, "hrv", hrv_values, hrv_value_count);
    apply_series_values(metrics, snapshot.metric_count, "temp", temp_values, temp_value_count);

    double motion_average = 0;
    apply_series_values(metrics, snapshot.metric_count, "motion", motion_values, motion_value_count, &motion_average);
    if (motion_value_count > 0) {
        set_metric_text(metrics, snapshot.metric_count, "motion", motion_average < 1.5 ? "LOW" : (motion_average < 3.5 ? "MOD" : "HIGH"));
        set_metric_number(metrics, snapshot.metric_count, "movements", motion_value_count);
    }

    double glucose_average = 0;
    double glucose_variation = 0;
    double time_in_target = 0;
    apply_series_values(metrics, snapshot.metric_count, "glucose", glucose_values, glucose_value_count,
                        &glucose_average, &glucose_variation, &time_in_target);
    if (glucose_value_count > 0) {
        set_metric_number(metrics, snapshot.metric_count, "glucose", glucose_values[glucose_value_count - 1]);
        set_metric_number(metrics, snapshot.metric_count, "average_glucose", glucose_average);
        set_metric_number(metrics, snapshot.metric_count, "glucose_variability", glucose_variation);
        set_metric_number(metrics, snapshot.metric_count, "time_in_target", time_in_target);
    }

    state_replace_health(metrics, snapshot.metric_count, static_cast<uint32_t>(std::time(nullptr)));
    size_t available_count = 0;
    for (size_t index = 0; index < snapshot.metric_count; ++index) {
        if (metrics[index].available) ++available_count;
    }
    ESP_LOGI(TAG, "Health synchronized (%u/%u metrics, series HR/HRV/temp %u/%u/%u, %u bytes)",
             static_cast<unsigned>(available_count), static_cast<unsigned>(snapshot.metric_count),
             static_cast<unsigned>(hr_value_count), static_cast<unsigned>(hrv_value_count),
             static_cast<unsigned>(temp_value_count), static_cast<unsigned>(body.size()));
    cJSON_Delete(root);
    return true;
}

bool refresh_rooms()
{
    if (!ensure_login()) return false;
    HttpResponse response;
    if (!http_request(HTTP_METHOD_GET, "/api/rooms", nullptr, true, response)) return false;
    if (response.status == 401) {
        s_access_token[0] = '\0';
        return false;
    }
    return response.status == 200 && parse_rooms(response.body);
}

bool refresh_health()
{
    if (!ensure_login()) return false;
    HttpResponse response;
    if (!http_request(HTTP_METHOD_GET, "/api/health/dashboard?days=1", nullptr, true, response)) return false;
    if (response.status == 401) {
        s_access_token[0] = '\0';
        return false;
    }
    return response.status == 200 && parse_health(response.body);
}

bool send_toggle(const ToggleRequest &request)
{
    if (!ensure_login()) return false;
    std::string path_value = "/api/devices/";
    path_value += request.device_id;
    path_value += "/set";
    const char *body = request.desired_state ? "{\"state\":true}" : "{\"state\":false}";
    HttpResponse response;
    if (!http_request(HTTP_METHOD_POST, path_value.c_str(), body, true, response)) return false;
    if (response.status == 401) {
        s_access_token[0] = '\0';
        return false;
    }
    if (response.status != 200) return false;
    cJSON *root = cJSON_ParseWithLength(response.body.data(), response.body.size());
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    const cJSON *is_on = cJSON_GetObjectItemCaseSensitive(state, "is_on");
    const bool actual = cJSON_IsBool(is_on) ? cJSON_IsTrue(is_on) : request.desired_state;
    cJSON_Delete(root);
    state_set_device(request.device_id, actual, false);
    return true;
}

void network_task(void *)
{
    uint32_t last_room_refresh = 0;
    uint32_t last_health_refresh = 0;
    uint32_t last_cache_save = 0;
    bool cache_dirty = false;
    bool clock_ready = false;
    while (true) {
        const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
        if ((bits & WIFI_CONNECTED_BIT) == 0) continue;
        if (!clock_ready) {
            clock_ready = wait_for_valid_clock();
            if (!clock_ready) continue;
        }

        ToggleRequest request{};
        while (xQueueReceive(s_toggle_queue, &request, 0) == pdTRUE) {
            if (!send_toggle(request)) {
                state_set_device(request.device_id, request.previous_state, false);
            }
        }

        const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        const bool forced = s_force_refresh;
        if (forced || last_room_refresh == 0 || now - last_room_refresh >= VOKRR_ROOM_REFRESH_MS) {
            last_room_refresh = now;
            if (refresh_rooms()) {
                cache_dirty = true;
            } else {
                ESP_LOGW(TAG, "Room synchronization failed; retaining cached state");
            }
        }
        if (forced || last_health_refresh == 0 || now - last_health_refresh >= VOKRR_HEALTH_REFRESH_MS) {
            last_health_refresh = now;
            if (refresh_health()) {
                cache_dirty = true;
            } else {
                ESP_LOGW(TAG, "Health synchronization failed; retaining cached state");
            }
        }
        if (cache_dirty && (last_cache_save == 0 || now - last_cache_save >= VOKRR_CACHE_SAVE_MS)) {
            state_save_cache();
            last_cache_save = now;
            cache_dirty = false;
        }
        s_force_refresh = false;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

}  // namespace

void network_start()
{
    if (s_network_task || VOKRR_PRIMARY_SSID[0] == '\0') return;

    s_wifi_events = xEventGroupCreate();
    s_toggle_queue = xQueueCreate(TOGGLE_QUEUE_DEPTH, sizeof(ToggleRequest));
    if (!s_wifi_events || !s_toggle_queue) {
        ESP_LOGE(TAG, "Failed to allocate network queues");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    const esp_err_t event_loop_result = esp_event_loop_create_default();
    if (event_loop_result != ESP_OK && event_loop_result != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(event_loop_result);
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    const esp_err_t wifi_init_result = esp_wifi_init(&init);
    if (wifi_init_result != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed: %s (internal free %u, largest %u)",
                 esp_err_to_name(wifi_init_result),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        return;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    configure_wifi_profile(0);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi initialized (internal free %u, largest %u)",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));

    if (xTaskCreatePinnedToCore(network_task, "vokrr_net", 12288, nullptr, 3, &s_network_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start network task");
        s_network_task = nullptr;
    }
}

void network_request_refresh()
{
    s_force_refresh = true;
}

bool network_toggle_device(const char *device_id, bool desired_state)
{
    if (!device_id || !s_toggle_queue) return false;
    ToggleRequest request{};
    copy_text(request.device_id, device_id);
    request.desired_state = desired_state;
    if (!state_set_device(device_id, desired_state, true, &request.previous_state)) return false;
    if (xQueueSend(s_toggle_queue, &request, 0) != pdTRUE) {
        state_set_device(device_id, request.previous_state, false);
        return false;
    }
    return true;
}

}  // namespace vokrr
