/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <new>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Health"

#include "esp_lib_utils.h"
#include "esp_brookesia.hpp"
#include "systems/phone/esp_brookesia_phone.hpp"
#include "systems/phone/assets/esp_brookesia_phone_assets.h"
#include "gui/lvgl/esp_brookesia_lv_lock.hpp"
#include "health_app.h"
#include "voice_assistant.h"
#include "watch_theme.h"

LV_IMG_DECLARE(quantum_watch_health_app_icon_98_98);

namespace esp_brookesia::apps {

using namespace esp_brookesia::systems;
using namespace esp_brookesia::gui;

#define APP_NAME "Health"
#define COLOR_BG             WATCH_COLOR_BG
#define COLOR_CARD           WATCH_COLOR_SURFACE
#define COLOR_CARD_ALT       WATCH_COLOR_SURFACE_ALT
#define COLOR_ACCENT         WATCH_COLOR_ACCENT
#define COLOR_DANGER         WATCH_COLOR_DANGER
#define COLOR_TEXT           WATCH_COLOR_TEXT
#define COLOR_TEXT_SECONDARY WATCH_COLOR_TEXT_MUTED

struct HealthPrivate {
    lv_obj_t *screen = nullptr;
    lv_obj_t *main_cont = nullptr;
    lv_obj_t *title_label = nullptr;
    lv_obj_t *updated_label = nullptr;
    lv_obj_t *status_label = nullptr;
    lv_timer_t *retry_timer = nullptr;
    std::array<lv_obj_t *, 4> value_labels = {nullptr, nullptr, nullptr, nullptr};
    std::array<lv_obj_t *, 4> title_labels = {nullptr, nullptr, nullptr, nullptr};
    std::string rx_buffer;
    std::string latest_payload;
    bool refresh_in_flight = false;
    uint8_t retry_count = 0;
};

static HealthPrivate *s_priv = nullptr;
HealthApp *HealthApp::_instance = nullptr;
static SemaphoreHandle_t s_health_mutex = nullptr;

static bool health_lv_attached(void)
{
    return s_priv && s_priv->screen && lv_obj_is_valid(s_priv->screen);
}

static void health_ui_detach(void)
{
    if (!s_priv) {
        return;
    }
    s_priv->screen = nullptr;
    s_priv->main_cont = nullptr;
    s_priv->title_label = nullptr;
    s_priv->updated_label = nullptr;
    s_priv->status_label = nullptr;
    s_priv->retry_timer = nullptr;
    s_priv->value_labels = {nullptr, nullptr, nullptr, nullptr};
    s_priv->title_labels = {nullptr, nullptr, nullptr, nullptr};
}

static bool health_lock(TickType_t timeout = portMAX_DELAY)
{
    return (s_health_mutex != nullptr) && (xSemaphoreTake(s_health_mutex, timeout) == pdTRUE);
}

static void health_unlock(void)
{
    if (s_health_mutex != nullptr) {
        xSemaphoreGive(s_health_mutex);
    }
}

static std::vector<std::string> split_payload(const std::string &payload)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= payload.size()) {
        size_t end = payload.find('|', start);
        if (end == std::string::npos) {
            parts.push_back(payload.substr(start));
            break;
        }
        parts.push_back(payload.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

static void set_status_label(const char *text, lv_color_t color)
{
    if (!s_priv || !s_priv->status_label) {
        return;
    }
    lv_label_set_text(s_priv->status_label, text);
    lv_obj_set_style_text_color(s_priv->status_label, color, 0);
}

static void set_status_text(const char *text)
{
    set_status_label(text, lv_color_hex(COLOR_TEXT_SECONDARY));
}

static void set_error_text(const char *reason)
{
    if (!reason) {
        set_status_label("", lv_color_hex(COLOR_DANGER));
        return;
    }

    std::string message = std::string("Failed to fetch data from iPhone: ") + reason;
    set_status_label(message.c_str(), lv_color_hex(COLOR_DANGER));
}

static void set_metric_label(size_t index, const char *text)
{
    if (!s_priv || index >= s_priv->value_labels.size()) {
        return;
    }
    lv_obj_t *label = s_priv->value_labels[index];
    if (label) {
        lv_label_set_text(label, text);
    }
}

static bool request_health_refresh_locked(void);

static void stop_retry_timer_locked(void)
{
    if (s_priv && s_priv->retry_timer) {
        lv_timer_delete(s_priv->retry_timer);
        s_priv->retry_timer = nullptr;
    }
}

static void retry_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_priv || !health_lv_attached()) {
        return;
    }

    if (request_health_refresh_locked()) {
        stop_retry_timer_locked();
        s_priv->retry_count = 0;
        return;
    }

    s_priv->retry_count++;
    if (s_priv->retry_count >= 3) {
        if (s_priv->latest_payload.empty()) {
            set_error_text("phone app not ready");
        } else {
            set_status_text("");
        }
        stop_retry_timer_locked();
    }
}

static void schedule_retry_locked(void)
{
    if (!s_priv || !health_lv_attached()) {
        return;
    }

    if (s_priv->retry_timer == nullptr) {
        s_priv->retry_timer = lv_timer_create(retry_timer_cb, 800, nullptr);
    } else {
        lv_timer_resume(s_priv->retry_timer);
    }
}

static void apply_latest_payload_locked(void)
{
    if (!s_priv) {
        return;
    }

    std::string payload;
    if (health_lock(pdMS_TO_TICKS(20))) {
        payload = s_priv->latest_payload;
        health_unlock();
    }

    std::vector<std::string> parts = split_payload(payload);
    if (parts.size() < 5) {
        set_error_text("invalid response");
        ESP_UTILS_LOGW("Health payload invalid: %s", payload.c_str());
        return;
    }

    ESP_UTILS_LOGI("Health payload received: %s", payload.c_str());
    stop_retry_timer_locked();
    s_priv->retry_count = 0;
    set_metric_label(0, parts[0].c_str());
    set_metric_label(1, parts[1].c_str());
    set_metric_label(2, parts[2].c_str());
    set_metric_label(3, parts[3].c_str());
    if (s_priv->updated_label) {
        std::string updated = std::string("Today ") + parts[4];
        lv_label_set_text(s_priv->updated_label, updated.c_str());
    }
    set_status_text("");
}

static bool request_health_refresh_locked(void)
{
    if (!va_ble_is_connected()) {
        stop_retry_timer_locked();
        set_error_text("phone disconnected");
        return false;
    }
    if (health_lock(pdMS_TO_TICKS(20))) {
        if (s_priv->refresh_in_flight) {
            health_unlock();
            set_status_text("Getting data from iPhone...");
            return true;
        }
        s_priv->refresh_in_flight = true;
        health_unlock();
    }
    if (!va_request_health_refresh()) {
        if (health_lock(pdMS_TO_TICKS(20))) {
            s_priv->refresh_in_flight = false;
            health_unlock();
        }
        if (!s_priv->latest_payload.empty()) {
            set_status_text("");
        } else {
            set_status_text("Getting data from iPhone...");
        }
        if (s_priv->latest_payload.empty()) {
            schedule_retry_locked();
        }
        return false;
    }
    stop_retry_timer_locked();
    s_priv->retry_count = 0;
    set_status_text("Getting data from iPhone...");
    return true;
}

static lv_obj_t *create_card(lv_obj_t *parent, const char *title, int col, int row)
{
    const int card_w = 172;
    const int card_h = 118;
    const int start_x = 18;
    const int start_y = 132;
    const int gap_x = 12;
    const int gap_y = 12;

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_set_pos(card, start_x + col * (card_w + gap_x), start_y + row * (card_h + gap_y));
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_CARD_ALT), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    size_t index = static_cast<size_t>(row * 2 + col);
    s_priv->title_labels[index] = title_label;

    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, "--");
    lv_obj_set_style_text_color(value_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_28, 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    s_priv->value_labels[index] = value_label;

    return card;
}

static void create_ui(lv_obj_t *screen)
{
    lv_obj_t *cont = lv_obj_create(screen);
    s_priv->main_cont = cont;
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_outline_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "Health");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    s_priv->title_label = title;

    s_priv->updated_label = lv_label_create(cont);
    lv_label_set_text(s_priv->updated_label, "Today --");
    lv_obj_set_style_text_color(s_priv->updated_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(s_priv->updated_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_priv->updated_label, 320);
    lv_obj_set_style_text_align(s_priv->updated_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_priv->updated_label, LV_ALIGN_BOTTOM_MID, 0, -18);

    create_card(cont, "Steps", 0, 0);
    create_card(cont, "Active", 1, 0);
    create_card(cont, "Distance", 0, 1);
    create_card(cont, "Heart", 1, 1);

    s_priv->status_label = lv_label_create(cont);
    lv_label_set_text(s_priv->status_label, "");
    lv_label_set_long_mode(s_priv->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_priv->status_label, 320);
    lv_obj_set_style_text_align(s_priv->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_priv->status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(s_priv->status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_priv->status_label, LV_ALIGN_BOTTOM_MID, 0, -42);

    if (!s_priv->latest_payload.empty()) {
        apply_latest_payload_locked();
    }
}

HealthApp *HealthApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new HealthApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

HealthApp::HealthApp(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME, &quantum_watch_health_app_icon_98_98, true, use_status_bar, use_navigation_bar)
{
    s_priv = new HealthPrivate();
    if (s_health_mutex == nullptr) {
        s_health_mutex = xSemaphoreCreateMutex();
    }
}

HealthApp::~HealthApp()
{
    delete s_priv;
    s_priv = nullptr;
    _instance = nullptr;
}

bool HealthApp::run(void)
{
    lv_obj_t *screen = lv_scr_act();
    if (!screen || !s_priv) {
        return false;
    }

    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_outline_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_coord_t dw = lv_display_get_horizontal_resolution(disp);
        lv_coord_t dh = lv_display_get_vertical_resolution(disp);
        if (dw > 0 && dh > 0) {
            lv_obj_set_size(screen, dw, dh);
        }
    }

    s_priv->screen = screen;
    create_ui(screen);
    request_health_refresh_locked();
    return true;
}

bool HealthApp::resume(void)
{
    if (health_lv_attached() && !s_priv->latest_payload.empty()) {
        apply_latest_payload_locked();
    }
    return true;
}

bool HealthApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool HealthApp::close(void)
{
    if (s_priv && s_priv->retry_timer) {
        lv_timer_delete(s_priv->retry_timer);
        s_priv->retry_timer = nullptr;
    }
    health_ui_detach();
    return true;
}

void health_app_on_ble_data_chunk(const uint8_t *data, uint16_t len, bool final)
{
    if (!s_priv) {
        return;
    }

    if (!health_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    if (data && len > 0) {
        s_priv->rx_buffer.append(reinterpret_cast<const char *>(data), len);
    }
    if (!final) {
        if (health_lv_attached()) {
            LvLockGuard gui_guard;
            if (health_lv_attached()) {
                set_status_text("Getting data from iPhone...");
            }
        }
        health_unlock();
        return;
    }

    s_priv->latest_payload = s_priv->rx_buffer;
    s_priv->rx_buffer.clear();
    s_priv->refresh_in_flight = false;
    health_unlock();

    ESP_UTILS_LOGI("Health reply complete");
    if (!health_lv_attached()) {
        return;
    }

    LvLockGuard gui_guard;
    if (health_lv_attached()) {
        apply_latest_payload_locked();
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, HealthApp, APP_NAME, []() {
    return std::shared_ptr<HealthApp>(HealthApp::requestInstance(), [](HealthApp *p) {});
})

} // namespace esp_brookesia::apps
