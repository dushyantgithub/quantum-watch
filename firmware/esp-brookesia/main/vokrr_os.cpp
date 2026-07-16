/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 * SPDX-License-Identifier: MIT
 */

#include "vokrr_os.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "buttons.h"
#include "esp_err.h"
#include "esp_log.h"
#include "bsp/display.h"
#include "esp_flash.h"
#include "lvgl.h"

#include "vokrr_config.h"
#include "vokrr_network.h"
#include "vokrr_state.h"
#include "voice_assistant.h"
#include "watch_theme.h"

LV_FONT_DECLARE(vokrr_font_jost_84);
LV_FONT_DECLARE(vokrr_font_jost_30);
LV_FONT_DECLARE(vokrr_font_jost_21);
LV_FONT_DECLARE(vokrr_font_jost_15);
LV_FONT_DECLARE(vokrr_font_mono_10);
LV_FONT_DECLARE(vokrr_font_mono_8);

namespace vokrr {
namespace {

constexpr int SCREEN_W = 410;
constexpr int SCREEN_H = 502;
constexpr int PAGE_COUNT = 6;
constexpr int PAGE_ANIMATION_MS = 450;
constexpr lv_opa_t OPA_05 = 13;

struct Ui {
    lv_obj_t *root = nullptr;
    lv_obj_t *track = nullptr;
    lv_obj_t *pages[PAGE_COUNT]{};
    lv_obj_t *dots[PAGE_COUNT]{};
    uint8_t page = 5;
    lv_point_t press_point{};
    bool pointer_down = false;

    Snapshot snapshot{};
    uint32_t rendered_revision = 0;
    uint32_t room_hash = 0;

    lv_obj_t *dashboard_link = nullptr;
    lv_obj_t *dashboard_time = nullptr;
    lv_obj_t *dashboard_suffix = nullptr;
    lv_obj_t *dashboard_date = nullptr;
    lv_obj_t *dashboard_hr = nullptr;
    lv_obj_t *dashboard_steps = nullptr;
    lv_obj_t *dashboard_recovery = nullptr;
    lv_obj_t *dashboard_battery = nullptr;
    lv_obj_t *dashboard_home = nullptr;

    lv_obj_t *health_values[MAX_HEALTH_METRICS]{};
    lv_obj_t *health_units[MAX_HEALTH_METRICS]{};
    lv_obj_t *health_lines[MAX_HEALTH_METRICS]{};
    lv_point_precise_t health_points[MAX_HEALTH_METRICS][SPARK_POINTS]{};

    lv_obj_t *rooms_scroll = nullptr;
    int open_room = -1;

    lv_obj_t *ai_link = nullptr;
    lv_obj_t *ai_chat = nullptr;
    lv_obj_t *ai_prompt = nullptr;
    lv_obj_t *ai_mic = nullptr;
    lv_obj_t *wave_bars[5]{};
    bool listening = false;
    bool processing = false;

    lv_obj_t *setting_wifi_network = nullptr;
    lv_obj_t *setting_wifi_signal = nullptr;
    lv_obj_t *setting_wifi_ip = nullptr;
    lv_obj_t *setting_bt_status = nullptr;
    lv_obj_t *setting_bt_device = nullptr;
    lv_obj_t *setting_ios_link = nullptr;
    lv_obj_t *setting_ios_sync = nullptr;
    lv_obj_t *setting_storage = nullptr;
    lv_obj_t *setting_battery = nullptr;
    lv_obj_t *setting_battery_health = nullptr;
    lv_obj_t *setting_battery_bar = nullptr;
    lv_obj_t *setting_battery_meta = nullptr;

    lv_obj_t *away_arcs[3]{};
    lv_obj_t *away_core = nullptr;
    lv_obj_t *away_time = nullptr;
    lv_obj_t *away_status = nullptr;

    lv_timer_t *update_timer = nullptr;
    lv_timer_t *motion_timer = nullptr;
} s_ui;

lv_color_t color(uint32_t value) { return lv_color_hex(value); }

void clean_object(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t text_color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color(text_color), 0);
    return label;
}

void enable_event_bubble(lv_obj_t *object)
{
    lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
}

lv_obj_t *make_page(uint8_t page)
{
    lv_obj_t *object = lv_obj_create(s_ui.track);
    clean_object(object);
    lv_obj_set_size(object, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(object, page * SCREEN_W, 0);
    lv_obj_set_style_bg_color(object, color(WATCH_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    enable_event_bubble(object);
    s_ui.pages[page] = object;
    return object;
}

int count_devices_on(const Snapshot &snapshot)
{
    int count = 0;
    for (size_t room = 0; room < snapshot.room_count; ++room) {
        for (size_t device = 0; device < snapshot.rooms[room].device_count; ++device) {
            if (snapshot.rooms[room].devices[device].is_on) ++count;
        }
    }
    return count;
}

int metric_index(const char *key) { return state_metric_index(s_ui.snapshot, key); }

const char *metric_value(const char *key, const char *fallback = "--")
{
    const int index = metric_index(key);
    return index >= 0 && s_ui.snapshot.metrics[index].available ? s_ui.snapshot.metrics[index].value : fallback;
}

void track_x_cb(void *object, int32_t x)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(object), x);
}

void update_dots()
{
    for (int index = 0; index < PAGE_COUNT; ++index) {
        lv_obj_set_style_bg_color(s_ui.dots[index], color(index == s_ui.page ? WATCH_COLOR_ACCENT : WATCH_COLOR_CHAMPAGNE), 0);
        lv_obj_set_style_bg_opa(s_ui.dots[index], index == s_ui.page ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

void set_page(uint8_t page, bool animated)
{
    page = std::min<uint8_t>(page, PAGE_COUNT - 1);
    if (!s_ui.track) return;
    lv_anim_delete(s_ui.track, track_x_cb);
    const int target_x = -static_cast<int>(page) * SCREEN_W;
    if (animated && page != s_ui.page) {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, s_ui.track);
        lv_anim_set_exec_cb(&animation, track_x_cb);
        lv_anim_set_values(&animation, lv_obj_get_x(s_ui.track), target_x);
        lv_anim_set_time(&animation, PAGE_ANIMATION_MS);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_start(&animation);
    } else {
        lv_obj_set_x(s_ui.track, target_x);
    }
    s_ui.page = page;
    update_dots();
    buttons_set_away_mode(page == 5);
    bsp_display_brightness_set(page == 5 ? VOKRR_AOD_BRIGHTNESS : VOKRR_ACTIVE_BRIGHTNESS);
}

void gesture_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *input = lv_indev_active();
    if (!input) return;
    lv_point_t point{};
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        s_ui.press_point = point;
        s_ui.pointer_down = true;
    } else if (code == LV_EVENT_RELEASED && s_ui.pointer_down) {
        s_ui.pointer_down = false;
        const int dx = point.x - s_ui.press_point.x;
        const int dy = point.y - s_ui.press_point.y;
        if (std::abs(dx) > 55 && std::abs(dx) * 10 > std::abs(dy) * 14) {
            // Away is a wake screen, not the end of the carousel. Either
            // horizontal direction wakes directly to the dashboard.
            const int next = s_ui.page == 5
                ? 0
                : std::clamp(static_cast<int>(s_ui.page) + (dx < 0 ? 1 : -1), 0, PAGE_COUNT - 2);
            ESP_LOGI("VokrrNav", "Swipe dx=%d dy=%d: page %u -> %d", dx, dy, s_ui.page, next);
            set_page(static_cast<uint8_t>(next), true);
        } else if (s_ui.page == 5 && std::abs(dx) < 18 && std::abs(dy) < 18) {
            ESP_LOGI("VokrrNav", "Away tap: page 5 -> 0");
            set_page(0, true);
        }
    }
}

void register_navigation_input()
{
    // Listen on the pointer device itself. Object-level RELEASED events are
    // swallowed by the wide page track and by vertically scrollable content.
    // Indev events always see the complete touch, independent of hit target.
    lv_indev_t *input = lv_indev_get_next(nullptr);
    while (input) {
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER) {
            lv_indev_add_event_cb(input, gesture_event, LV_EVENT_PRESSED, nullptr);
            lv_indev_add_event_cb(input, gesture_event, LV_EVENT_RELEASED, nullptr);
            ESP_LOGI("VokrrNav", "Page navigation registered on input %p", static_cast<void *>(input));
            return;
        }
        input = lv_indev_get_next(input);
    }
    ESP_LOGW("VokrrNav", "No pointer input found; page navigation unavailable");
}

void attach_gesture(lv_obj_t *object)
{
    enable_event_bubble(object);
}

void make_hairline(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *line = lv_obj_create(parent);
    clean_object(line);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, width, height);
    lv_obj_set_style_bg_color(line, color(WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_10, 0);
}

void create_dashboard()
{
    lv_obj_t *page = make_page(0);

    lv_obj_t *status = lv_obj_create(page);
    clean_object(status);
    lv_obj_set_size(status, 220, 20);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 43);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 11, 0);
    make_label(status, LV_SYMBOL_WIFI, &lv_font_montserrat_14, WATCH_COLOR_CHAMPAGNE);
    make_label(status, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_14, WATCH_COLOR_CHAMPAGNE);
    s_ui.dashboard_link = make_label(status, "* LINKED", &vokrr_font_mono_8, WATCH_COLOR_ACCENT);
    lv_obj_set_style_text_letter_space(s_ui.dashboard_link, 2, 0);

    s_ui.dashboard_time = make_label(page, "--:--", &vokrr_font_jost_84, WATCH_COLOR_TEXT);
    lv_obj_set_width(s_ui.dashboard_time, 310);
    lv_obj_set_style_text_align(s_ui.dashboard_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.dashboard_time, LV_ALIGN_TOP_MID, -6, 116);
    s_ui.dashboard_suffix = make_label(page, "", &vokrr_font_mono_10, WATCH_COLOR_CHAMPAGNE);
    lv_obj_align(s_ui.dashboard_suffix, LV_ALIGN_TOP_RIGHT, -38, 196);
    s_ui.dashboard_date = make_label(page, "---  --", &vokrr_font_mono_10, WATCH_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(s_ui.dashboard_date, 3, 0);
    lv_obj_align(s_ui.dashboard_date, LV_ALIGN_TOP_MID, 0, 232);

    make_hairline(page, 34, 292, 342, 1);
    make_hairline(page, 34, 386, 342, 1);
    make_hairline(page, 148, 307, 1, 64);
    make_hairline(page, 262, 307, 1, 64);

    struct Complication { const char *icon; const char *label; uint32_t icon_color; lv_obj_t **value; };
    Complication complications[] = {
        {"HR", "BPM", WATCH_COLOR_ACCENT, &s_ui.dashboard_hr},
        {LV_SYMBOL_CHARGE, "STEPS", WATCH_COLOR_CHAMPAGNE, &s_ui.dashboard_steps},
        {LV_SYMBOL_OK, "RECOVERY", WATCH_COLOR_ACCENT, &s_ui.dashboard_recovery},
    };
    for (int index = 0; index < 3; ++index) {
        const int center = 91 + index * 114;
        lv_obj_t *icon = make_label(page, complications[index].icon, &lv_font_montserrat_14, complications[index].icon_color);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, center - 7, 305);
        *complications[index].value = make_label(page, "--", &vokrr_font_jost_21, WATCH_COLOR_TEXT);
        lv_obj_set_width(*complications[index].value, 100);
        lv_obj_set_style_text_align(*complications[index].value, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(*complications[index].value, LV_ALIGN_TOP_LEFT, center - 50, 329);
        lv_obj_t *label = make_label(page, complications[index].label, &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);
        lv_obj_set_style_text_letter_space(label, 2, 0);
        lv_obj_set_width(label, 100);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, center - 50, 357);
    }

    s_ui.dashboard_battery = make_label(page, "BAT --%", &vokrr_font_mono_8, WATCH_COLOR_TEXT_MUTED);
    lv_obj_align(s_ui.dashboard_battery, LV_ALIGN_TOP_LEFT, 72, 407);
    s_ui.dashboard_home = make_label(page, "VOKRR HOME  *  -- ON", &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(s_ui.dashboard_home, 1, 0);
    lv_obj_align(s_ui.dashboard_home, LV_ALIGN_TOP_LEFT, 170, 407);
}

const char *category_name(HealthCategory category)
{
    switch (category) {
        case HealthCategory::Vitals: return "VITALS";
        case HealthCategory::Glucose: return "GLUCOSE";
        case HealthCategory::Activity: return "ACTIVITY";
        case HealthCategory::Sleep: return "SLEEP";
        case HealthCategory::Recovery: return "RECOVERY";
    }
    return "";
}

void create_health()
{
    lv_obj_t *page = make_page(1);
    lv_obj_t *scroll = lv_obj_create(page);
    clean_object(scroll);
    lv_obj_set_size(scroll, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(scroll, 0, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_left(scroll, 44, 0);
    lv_obj_set_style_pad_right(scroll, 44, 0);
    lv_obj_set_style_pad_top(scroll, 42, 0);
    lv_obj_set_style_pad_bottom(scroll, 60, 0);
    lv_obj_set_style_pad_row(scroll, 0, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    attach_gesture(scroll);

    lv_obj_t *title_row = lv_obj_create(scroll);
    clean_object(title_row);
    lv_obj_set_width(title_row, lv_pct(100));
    lv_obj_set_height(title_row, 34);
    make_label(title_row, "Health", &vokrr_font_jost_30, WATCH_COLOR_TEXT);
    lv_obj_t *today = make_label(title_row, "TODAY", &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);
    lv_obj_align(today, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_letter_space(today, 2, 0);

    HealthCategory last_category = static_cast<HealthCategory>(255);
    for (size_t index = 0; index < s_ui.snapshot.metric_count; ++index) {
        const HealthMetric &metric = s_ui.snapshot.metrics[index];
        if (metric.category != last_category) {
            lv_obj_t *header = lv_obj_create(scroll);
            clean_object(header);
            lv_obj_set_width(header, lv_pct(100));
            lv_obj_set_height(header, 36);
            lv_obj_set_style_pad_top(header, 18, 0);
            lv_obj_t *dot = lv_obj_create(header);
            clean_object(dot);
            lv_obj_set_size(dot, 5, 5);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, color(metric.color), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 8);
            lv_obj_t *category = make_label(header, category_name(metric.category), &vokrr_font_mono_10, WATCH_COLOR_TEXT_MUTED);
            lv_obj_set_style_text_letter_space(category, 2, 0);
            lv_obj_align(category, LV_ALIGN_LEFT_MID, 13, 8);
            last_category = metric.category;
        }

        lv_obj_t *row = lv_obj_create(scroll);
        clean_object(row);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 58);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, color(WATCH_COLOR_CHAMPAGNE), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_10, 0);

        lv_obj_t *name = make_label(row, metric.label, &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);
        lv_obj_set_style_text_letter_space(name, 1, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 5);
        s_ui.health_values[index] = make_label(row, metric.value, &vokrr_font_jost_21, WATCH_COLOR_TEXT);
        lv_obj_align(s_ui.health_values[index], LV_ALIGN_TOP_LEFT, 0, 23);
        s_ui.health_units[index] = make_label(row, metric.unit, &vokrr_font_mono_8, WATCH_COLOR_TEXT_MUTED);
        lv_obj_align(s_ui.health_units[index], LV_ALIGN_TOP_LEFT, 86, 31);

        s_ui.health_lines[index] = lv_line_create(row);
        lv_obj_set_size(s_ui.health_lines[index], 84, 26);
        lv_obj_align(s_ui.health_lines[index], LV_ALIGN_RIGHT_MID, 0, 2);
        lv_obj_set_style_line_color(s_ui.health_lines[index], color(metric.color), 0);
        lv_obj_set_style_line_width(s_ui.health_lines[index], 1, 0);
        lv_obj_set_style_line_rounded(s_ui.health_lines[index], true, 0);
    }
}

uint32_t room_state_hash(const Snapshot &snapshot)
{
    uint32_t hash = 2166136261u;
    for (size_t room = 0; room < snapshot.room_count; ++room) {
        for (const char *text = snapshot.rooms[room].id; *text; ++text) hash = (hash ^ static_cast<uint8_t>(*text)) * 16777619u;
        for (size_t device = 0; device < snapshot.rooms[room].device_count; ++device) {
            hash = (hash ^ snapshot.rooms[room].devices[device].is_on) * 16777619u;
            hash = (hash ^ snapshot.rooms[room].devices[device].pending) * 16777619u;
        }
    }
    return hash;
}

void rebuild_rooms();

void room_open_cb(lv_event_t *event)
{
    const intptr_t encoded = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    s_ui.open_room = static_cast<int>(encoded - 1);
    rebuild_rooms();
}

void room_back_cb(lv_event_t *)
{
    s_ui.open_room = -1;
    rebuild_rooms();
}

void device_toggle_cb(lv_event_t *event)
{
    const intptr_t encoded = reinterpret_cast<intptr_t>(lv_event_get_user_data(event)) - 1;
    const int room_index = static_cast<int>(encoded / MAX_DEVICES_PER_ROOM);
    const int device_index = static_cast<int>(encoded % MAX_DEVICES_PER_ROOM);
    if (room_index < 0 || room_index >= s_ui.snapshot.room_count) return;
    const Room &room = s_ui.snapshot.rooms[room_index];
    if (device_index < 0 || device_index >= room.device_count) return;
    const Device &device = room.devices[device_index];
    network_toggle_device(device.id, !device.is_on);
    if (state_copy(s_ui.snapshot)) rebuild_rooms();
}

lv_obj_t *make_card(lv_obj_t *parent, int height = 52)
{
    lv_obj_t *card = lv_obj_create(parent);
    clean_object(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, height);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_bg_color(card, color(WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_bg_opa(card, OPA_05, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, color(WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_10, 0);
    return card;
}

void rebuild_rooms()
{
    if (!s_ui.rooms_scroll) return;
    lv_obj_clean(s_ui.rooms_scroll);
    lv_obj_set_flex_flow(s_ui.rooms_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.rooms_scroll, 8, 0);

    if (s_ui.open_room < 0 || s_ui.open_room >= s_ui.snapshot.room_count) {
        s_ui.open_room = -1;
        lv_obj_t *header = lv_obj_create(s_ui.rooms_scroll);
        clean_object(header);
        lv_obj_set_width(header, lv_pct(100));
        lv_obj_set_height(header, 38);
        make_label(header, "Rooms", &vokrr_font_jost_30, WATCH_COLOR_TEXT);
        char total[24];
        std::snprintf(total, sizeof(total), "%d ON", count_devices_on(s_ui.snapshot));
        lv_obj_t *count = make_label(header, total, &vokrr_font_mono_8, WATCH_COLOR_ACCENT);
        lv_obj_set_style_text_letter_space(count, 2, 0);
        lv_obj_align(count, LV_ALIGN_RIGHT_MID, 0, 0);

        for (size_t room_index = 0; room_index < s_ui.snapshot.room_count; ++room_index) {
            const Room &room = s_ui.snapshot.rooms[room_index];
            int on = 0;
            for (size_t device = 0; device < room.device_count; ++device) on += room.devices[device].is_on;
            lv_obj_t *card = make_card(s_ui.rooms_scroll, 52);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, room_open_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(room_index + 1));
            attach_gesture(card);
            lv_obj_t *icon = make_label(card, LV_SYMBOL_HOME, &lv_font_montserrat_16, WATCH_COLOR_CHAMPAGNE);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, 0);
            lv_obj_t *name = make_label(card, room.name, &vokrr_font_jost_15, WATCH_COLOR_TEXT);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 50, 0);
            char count_text[16];
            std::snprintf(count_text, sizeof(count_text), on ? "%d ON" : "OFF", on);
            lv_obj_t *count_label = make_label(card, count_text, &vokrr_font_mono_8, on ? WATCH_COLOR_ACCENT : WATCH_COLOR_TEXT_DIM);
            lv_obj_align(count_label, LV_ALIGN_RIGHT_MID, -28, 0);
            lv_obj_t *chevron = make_label(card, LV_SYMBOL_RIGHT, &lv_font_montserrat_12, WATCH_COLOR_TEXT_DIM);
            lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -10, 0);
        }
    } else {
        const Room &room = s_ui.snapshot.rooms[s_ui.open_room];
        int on = 0;
        for (size_t device = 0; device < room.device_count; ++device) on += room.devices[device].is_on;
        lv_obj_t *header = lv_obj_create(s_ui.rooms_scroll);
        clean_object(header);
        lv_obj_set_width(header, lv_pct(100));
        lv_obj_set_height(header, 44);
        lv_obj_t *back = lv_obj_create(header);
        clean_object(back);
        lv_obj_set_size(back, 32, 32);
        lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(back, 1, 0);
        lv_obj_set_style_border_color(back, color(WATCH_COLOR_CHAMPAGNE), 0);
        lv_obj_set_style_border_opa(back, LV_OPA_30, 0);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(back, room_back_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *back_icon = make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_14, WATCH_COLOR_CHAMPAGNE);
        lv_obj_center(back_icon);
        lv_obj_t *name = make_label(header, room.name, &vokrr_font_jost_21, WATCH_COLOR_TEXT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 44, 0);
        char subtitle[24];
        std::snprintf(subtitle, sizeof(subtitle), "%d OF %d ON", on, room.device_count);
        lv_obj_t *sub = make_label(header, subtitle, &vokrr_font_mono_8, WATCH_COLOR_ACCENT);
        lv_obj_set_style_text_letter_space(sub, 1, 0);
        lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 44, 25);

        for (size_t device_index = 0; device_index < room.device_count; ++device_index) {
            const Device &device = room.devices[device_index];
            lv_obj_t *card = make_card(s_ui.rooms_scroll, 58);
            lv_obj_set_style_bg_color(card, color(device.is_on ? WATCH_COLOR_ACCENT : WATCH_COLOR_CHAMPAGNE), 0);
            lv_obj_set_style_bg_opa(card, device.is_on ? static_cast<lv_opa_t>(LV_OPA_10) : OPA_05, 0);
            lv_obj_set_style_border_color(card, color(device.is_on ? WATCH_COLOR_ACCENT : WATCH_COLOR_CHAMPAGNE), 0);
            lv_obj_set_style_border_opa(card, device.is_on ? LV_OPA_30 : LV_OPA_10, 0);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            const intptr_t encoded = s_ui.open_room * MAX_DEVICES_PER_ROOM + device_index + 1;
            lv_obj_add_event_cb(card, device_toggle_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(encoded));
            attach_gesture(card);

            lv_obj_t *icon = make_label(card, LV_SYMBOL_POWER, &lv_font_montserrat_16, device.is_on ? WATCH_COLOR_ACCENT : WATCH_COLOR_TEXT_MUTED);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 15, 0);
            lv_obj_t *name = make_label(card, device.name, &vokrr_font_jost_15, WATCH_COLOR_TEXT);
            lv_obj_align(name, LV_ALIGN_TOP_LEFT, 49, 10);
            char sub_text[24];
            if (device.pending) std::snprintf(sub_text, sizeof(sub_text), "SYNCING");
            else if (device.is_on && device.watts) std::snprintf(sub_text, sizeof(sub_text), "ON  *  %uW", device.watts);
            else std::snprintf(sub_text, sizeof(sub_text), "%s", device.is_on ? "ON" : "OFF");
            lv_obj_t *sub = make_label(card, sub_text, &vokrr_font_mono_8, device.is_on ? WATCH_COLOR_ACCENT : WATCH_COLOR_TEXT_DIM);
            lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 49, 34);

            lv_obj_t *pill = lv_obj_create(card);
            clean_object(pill);
            lv_obj_set_size(pill, 36, 21);
            lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(pill, color(device.is_on ? WATCH_COLOR_ACCENT : WATCH_COLOR_CHAMPAGNE), 0);
            lv_obj_set_style_bg_opa(pill, device.is_on ? LV_OPA_COVER : LV_OPA_20, 0);
            lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -14, 0);
            lv_obj_t *knob = lv_obj_create(pill);
            clean_object(knob);
            lv_obj_set_size(knob, 15, 15);
            lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(knob, color(WATCH_COLOR_TEXT), 0);
            lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
            lv_obj_set_pos(knob, device.is_on ? 18 : 3, 3);
        }
    }
    s_ui.room_hash = room_state_hash(s_ui.snapshot);
}

void create_rooms()
{
    lv_obj_t *page = make_page(2);
    s_ui.rooms_scroll = lv_obj_create(page);
    clean_object(s_ui.rooms_scroll);
    lv_obj_set_size(s_ui.rooms_scroll, SCREEN_W, SCREEN_H);
    lv_obj_add_flag(s_ui.rooms_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_ui.rooms_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.rooms_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_left(s_ui.rooms_scroll, 40, 0);
    lv_obj_set_style_pad_right(s_ui.rooms_scroll, 40, 0);
    lv_obj_set_style_pad_top(s_ui.rooms_scroll, 42, 0);
    lv_obj_set_style_pad_bottom(s_ui.rooms_scroll, 60, 0);
    attach_gesture(s_ui.rooms_scroll);
    rebuild_rooms();
}

void add_chat_bubble(bool user, const char *text)
{
    if (!s_ui.ai_chat || !text || !*text) return;
    lv_obj_t *bubble = lv_obj_create(s_ui.ai_chat);
    clean_object(bubble);
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, user ? 250 : 264, 0);
    lv_obj_set_style_pad_hor(bubble, 14, 0);
    lv_obj_set_style_pad_ver(bubble, 9, 0);
    lv_obj_set_style_radius(bubble, 18, 0);
    lv_obj_set_style_bg_color(bubble, color(user ? WATCH_COLOR_ACCENT : WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_bg_opa(bubble, user ? LV_OPA_20 : LV_OPA_10, 0);
    lv_obj_set_style_border_width(bubble, 1, 0);
    lv_obj_set_style_border_color(bubble, color(user ? WATCH_COLOR_ACCENT : WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_border_opa(bubble, user ? LV_OPA_30 : LV_OPA_20, 0);
    lv_obj_set_align(bubble, user ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT);
    lv_obj_t *label = make_label(bubble, text, &vokrr_font_jost_15, user ? WATCH_COLOR_TEXT : 0xD9D9CF);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(label, user ? 220 : 234, 0);
    lv_obj_scroll_to_view(bubble, LV_ANIM_ON);
}

void mic_cb(lv_event_t *)
{
    if (!esp_brookesia::apps::va_ble_is_connected()) {
        add_chat_bubble(false, "iOS link unavailable. Open the Vokrr companion app.");
        return;
    }
    if (s_ui.listening) {
        esp_brookesia::apps::va_stop_listening();
        os_set_listening(false, true);
    } else if (esp_brookesia::apps::va_start_listening()) {
        os_set_listening(true, false);
    }
}

void create_ai()
{
    lv_obj_t *page = make_page(3);
    lv_obj_t *header = lv_obj_create(page);
    clean_object(header);
    lv_obj_set_size(header, 280, 18);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 7, 0);
    lv_obj_t *jarvis = make_label(header, "* JARVIS", &vokrr_font_mono_10, WATCH_COLOR_CHAMPAGNE);
    lv_obj_set_style_text_letter_space(jarvis, 3, 0);
    s_ui.ai_link = make_label(header, "* iOS LINK", &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);

    s_ui.ai_chat = lv_obj_create(page);
    clean_object(s_ui.ai_chat);
    lv_obj_set_size(s_ui.ai_chat, 322, 315);
    lv_obj_align(s_ui.ai_chat, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_add_flag(s_ui.ai_chat, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_ui.ai_chat, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.ai_chat, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_ui.ai_chat, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.ai_chat, 10, 0);
    attach_gesture(s_ui.ai_chat);
    add_chat_bubble(false, "Good afternoon. Home is secure. Ask me anything.");

    lv_obj_t *wave = lv_obj_create(page);
    clean_object(wave);
    lv_obj_set_size(wave, 40, 18);
    lv_obj_align(wave, LV_ALIGN_BOTTOM_MID, 0, -92);
    lv_obj_set_flex_flow(wave, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wave, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wave, 3, 0);
    for (int index = 0; index < 5; ++index) {
        s_ui.wave_bars[index] = lv_obj_create(wave);
        clean_object(s_ui.wave_bars[index]);
        lv_obj_set_size(s_ui.wave_bars[index], 3, 5);
        lv_obj_set_style_radius(s_ui.wave_bars[index], 2, 0);
        lv_obj_set_style_bg_color(s_ui.wave_bars[index], color(WATCH_COLOR_ACCENT), 0);
        lv_obj_set_style_bg_opa(s_ui.wave_bars[index], LV_OPA_COVER, 0);
        lv_obj_add_flag(s_ui.wave_bars[index], LV_OBJ_FLAG_HIDDEN);
    }
    s_ui.ai_prompt = make_label(page, "TAP TO SPEAK", &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(s_ui.ai_prompt, 2, 0);
    lv_obj_align(s_ui.ai_prompt, LV_ALIGN_BOTTOM_MID, 0, -91);

    s_ui.ai_mic = lv_obj_create(page);
    clean_object(s_ui.ai_mic);
    lv_obj_set_size(s_ui.ai_mic, 54, 54);
    lv_obj_set_style_radius(s_ui.ai_mic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.ai_mic, color(WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_bg_opa(s_ui.ai_mic, OPA_05, 0);
    lv_obj_set_style_border_width(s_ui.ai_mic, 1, 0);
    lv_obj_set_style_border_color(s_ui.ai_mic, color(WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_border_opa(s_ui.ai_mic, LV_OPA_30, 0);
    lv_obj_align(s_ui.ai_mic, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_obj_add_flag(s_ui.ai_mic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.ai_mic, mic_cb, LV_EVENT_CLICKED, nullptr);
    attach_gesture(s_ui.ai_mic);
    lv_obj_t *icon = make_label(s_ui.ai_mic, LV_SYMBOL_AUDIO, &lv_font_montserrat_20, WATCH_COLOR_CHAMPAGNE);
    lv_obj_center(icon);
}

lv_obj_t *settings_row(lv_obj_t *card, const char *key, const char *value, uint32_t value_color)
{
    lv_obj_t *row = lv_obj_create(card);
    clean_object(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 38);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, color(WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_10, 0);
    lv_obj_t *left = make_label(row, key, &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(left, 1, 0);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *right = make_label(row, value, &vokrr_font_jost_15, value_color);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
    return right;
}

lv_obj_t *settings_group(lv_obj_t *scroll, const char *title)
{
    lv_obj_t *group = lv_obj_create(scroll);
    clean_object(group);
    lv_obj_set_width(group, lv_pct(100));
    lv_obj_set_height(group, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(group, 6, 0);
    lv_obj_t *heading = make_label(group, title, &vokrr_font_mono_10, WATCH_COLOR_TEXT_MUTED);
    lv_obj_set_style_text_letter_space(heading, 2, 0);
    return group;
}

lv_obj_t *settings_card(lv_obj_t *group)
{
    lv_obj_t *card = make_card(group, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(card, 16, 0);
    lv_obj_set_style_pad_right(card, 16, 0);
    lv_obj_set_style_pad_top(card, 4, 0);
    lv_obj_set_style_pad_bottom(card, 4, 0);
    return card;
}

void create_settings()
{
    lv_obj_t *page = make_page(4);
    lv_obj_t *scroll = lv_obj_create(page);
    clean_object(scroll);
    lv_obj_set_size(scroll, SCREEN_W, SCREEN_H);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(scroll, 44, 0);
    lv_obj_set_style_pad_right(scroll, 44, 0);
    lv_obj_set_style_pad_top(scroll, 42, 0);
    lv_obj_set_style_pad_bottom(scroll, 60, 0);
    lv_obj_set_style_pad_row(scroll, 16, 0);
    attach_gesture(scroll);
    make_label(scroll, "Settings", &vokrr_font_jost_30, WATCH_COLOR_TEXT);

    lv_obj_t *group = settings_group(scroll, "WI-FI");
    lv_obj_t *card = settings_card(group);
    s_ui.setting_wifi_network = settings_row(card, "NETWORK", "--", WATCH_COLOR_TEXT);
    s_ui.setting_wifi_signal = settings_row(card, "SIGNAL", "OFFLINE", WATCH_COLOR_ACCENT);
    s_ui.setting_wifi_ip = settings_row(card, "IP ADDRESS", "--", WATCH_COLOR_TEXT_MUTED);

    group = settings_group(scroll, "BLUETOOTH");
    card = settings_card(group);
    s_ui.setting_bt_status = settings_row(card, "STATUS", "DISCONNECTED", WATCH_COLOR_ACCENT);
    s_ui.setting_bt_device = settings_row(card, "DEVICE", "iPhone", WATCH_COLOR_TEXT);
    settings_row(card, "PROTOCOL", "BLE 5.0", WATCH_COLOR_TEXT_MUTED);

    group = settings_group(scroll, "iOS APP");
    card = settings_card(group);
    s_ui.setting_ios_link = settings_row(card, "LINK", "INACTIVE", WATCH_COLOR_ACCENT);
    s_ui.setting_ios_sync = settings_row(card, "LAST SYNC", "--", WATCH_COLOR_TEXT_MUTED);
    settings_row(card, "APP VERSION", "VOKRR", WATCH_COLOR_TEXT_MUTED);

    group = settings_group(scroll, "SYSTEM");
    card = settings_card(group);
    settings_row(card, "OS", VOKRR_OS_NAME " " VOKRR_OS_VERSION, WATCH_COLOR_TEXT);
    settings_row(card, "CHIP", "ESP32-S3  *  240 MHZ", WATCH_COLOR_TEXT_MUTED);
    settings_row(card, "DISPLAY", "AMOLED 410x502", WATCH_COLOR_TEXT_MUTED);
    s_ui.setting_storage = settings_row(card, "STORAGE", "--", WATCH_COLOR_TEXT_MUTED);

    group = settings_group(scroll, "BATTERY");
    card = settings_card(group);
    lv_obj_set_style_pad_top(card, 14, 0);
    lv_obj_set_style_pad_bottom(card, 14, 0);
    s_ui.setting_battery = make_label(card, "--%", &vokrr_font_jost_30, WATCH_COLOR_TEXT);
    s_ui.setting_battery_health = make_label(card, "HEALTH 96%", &vokrr_font_mono_8, WATCH_COLOR_ACCENT);
    lv_obj_align(s_ui.setting_battery_health, LV_ALIGN_TOP_RIGHT, 0, 5);
    lv_obj_t *track = lv_obj_create(card);
    clean_object(track);
    lv_obj_set_size(track, lv_pct(100), 4);
    lv_obj_set_style_radius(track, 2, 0);
    lv_obj_set_style_bg_color(track, color(WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_10, 0);
    s_ui.setting_battery_bar = lv_obj_create(track);
    clean_object(s_ui.setting_battery_bar);
    lv_obj_set_size(s_ui.setting_battery_bar, 1, 4);
    lv_obj_set_style_radius(s_ui.setting_battery_bar, 2, 0);
    lv_obj_set_style_bg_color(s_ui.setting_battery_bar, color(WATCH_COLOR_ACCENT_LIGHT), 0);
    lv_obj_set_style_bg_opa(s_ui.setting_battery_bar, LV_OPA_COVER, 0);
    s_ui.setting_battery_meta = make_label(card, "EST. --  *  LAST CHARGE --", &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);
}

lv_obj_t *make_reactor_arc(lv_obj_t *parent, int size, uint32_t arc_color, int width, int start, int end)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 1, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color(WATCH_COLOR_CHAMPAGNE), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color(arc_color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_50, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, start, end);
    lv_obj_center(arc);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

void create_away()
{
    lv_obj_t *page = make_page(5);
    lv_obj_t *reactor = lv_obj_create(page);
    clean_object(reactor);
    lv_obj_set_size(reactor, 200, 200);
    lv_obj_align(reactor, LV_ALIGN_CENTER, 0, -26);

    s_ui.away_arcs[0] = make_reactor_arc(reactor, 200, WATCH_COLOR_ACCENT, 1, 10, 315);
    s_ui.away_arcs[1] = make_reactor_arc(reactor, 168, WATCH_COLOR_CHAMPAGNE, 1, 25, 270);
    s_ui.away_arcs[2] = make_reactor_arc(reactor, 132, WATCH_COLOR_ACCENT, 1, 40, 285);

    lv_obj_t *halo = lv_obj_create(reactor);
    clean_object(halo);
    lv_obj_set_size(halo, 116, 116);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(halo, color(WATCH_COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_10, 0);
    lv_obj_center(halo);
    s_ui.away_core = lv_obj_create(reactor);
    clean_object(s_ui.away_core);
    lv_obj_set_size(s_ui.away_core, 84, 84);
    lv_obj_set_style_radius(s_ui.away_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.away_core, color(WATCH_COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_ui.away_core, LV_OPA_30, 0);
    lv_obj_center(s_ui.away_core);

    s_ui.away_time = make_label(reactor, "--:--", &vokrr_font_jost_30, WATCH_COLOR_TEXT);
    lv_obj_set_style_text_letter_space(s_ui.away_time, 4, 0);
    lv_obj_center(s_ui.away_time);
    lv_obj_t *jarvis = make_label(page, "JARVIS", &vokrr_font_mono_10, WATCH_COLOR_CHAMPAGNE);
    lv_obj_set_style_text_letter_space(jarvis, 5, 0);
    lv_obj_align(jarvis, LV_ALIGN_CENTER, 0, 112);
    s_ui.away_status = make_label(page, "STANDBY  *  TOUCH TO WAKE", &vokrr_font_mono_8, WATCH_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(s_ui.away_status, 2, 0);
    lv_obj_align(s_ui.away_status, LV_ALIGN_CENTER, 0, 137);
}

void update_time()
{
    std::time_t now = std::time(nullptr);
    std::tm timeinfo{};
    localtime_r(&now, &timeinfo);
    char time_text[16];
    const int hour = timeinfo.tm_hour % 12 == 0 ? 12 : timeinfo.tm_hour % 12;
    std::snprintf(time_text, sizeof(time_text), "%d:%02d", hour, timeinfo.tm_min);
    lv_label_set_text(s_ui.dashboard_time, time_text);
    lv_label_set_text(s_ui.away_time, time_text);
    lv_label_set_text(s_ui.dashboard_suffix, timeinfo.tm_hour < 12 ? "AM" : "PM");
    static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    char date[32];
    std::snprintf(date, sizeof(date), "%s  *  %s %d", days[timeinfo.tm_wday], months[timeinfo.tm_mon], timeinfo.tm_mday);
    lv_label_set_text(s_ui.dashboard_date, date);
}

void update_health_rows()
{
    for (size_t index = 0; index < s_ui.snapshot.metric_count; ++index) {
        const HealthMetric &metric = s_ui.snapshot.metrics[index];
        lv_label_set_text(s_ui.health_values[index], metric.available ? metric.value : "--");
        lv_label_set_text(s_ui.health_units[index], metric.available ? metric.unit : "");
        int minimum = metric.spark[0];
        int maximum = metric.spark[0];
        for (size_t point = 1; point < SPARK_POINTS; ++point) {
            minimum = std::min(minimum, static_cast<int>(metric.spark[point]));
            maximum = std::max(maximum, static_cast<int>(metric.spark[point]));
        }
        const int span = std::max(1, maximum - minimum);
        for (size_t point = 0; point < SPARK_POINTS; ++point) {
            s_ui.health_points[index][point].x = static_cast<lv_coord_t>(point * 80 / (SPARK_POINTS - 1));
            s_ui.health_points[index][point].y = static_cast<lv_coord_t>(24 - ((metric.spark[point] - minimum) * 20 / span));
        }
        lv_line_set_points(s_ui.health_lines[index], s_ui.health_points[index], SPARK_POINTS);
    }
}

const char *signal_quality(int rssi)
{
    if (rssi >= -55) return "Strong";
    if (rssi >= -67) return "Good";
    if (rssi >= -75) return "Fair";
    return "Weak";
}

void format_ago(uint32_t epoch, char *buffer, size_t capacity)
{
    if (epoch == 0) {
        std::snprintf(buffer, capacity, "--");
        return;
    }
    const std::time_t now = std::time(nullptr);
    const uint32_t seconds = now > epoch ? static_cast<uint32_t>(now - epoch) : 0;
    if (seconds < 60) std::snprintf(buffer, capacity, "JUST NOW");
    else if (seconds < 3600) std::snprintf(buffer, capacity, "%lu MIN AGO", static_cast<unsigned long>(seconds / 60));
    else std::snprintf(buffer, capacity, "%lu H AGO", static_cast<unsigned long>(seconds / 3600));
}

void update_live_data()
{
    const bool ble_connected = esp_brookesia::apps::va_ble_is_connected();
    if (ble_connected != s_ui.snapshot.ble_connected) {
        state_set_ble(ble_connected, ble_connected ? "iPhone" : nullptr);
    }
    if (!state_copy(s_ui.snapshot)) return;
    const Snapshot &latest = s_ui.snapshot;
    const bool changed = latest.revision != s_ui.rendered_revision;
    if (!changed) return;
    s_ui.rendered_revision = latest.revision;

    lv_label_set_text(s_ui.dashboard_link, latest.ble_connected ? "* LINKED" : "* OFFLINE");
    lv_obj_set_style_text_color(s_ui.dashboard_link, color(latest.ble_connected ? WATCH_COLOR_ACCENT : WATCH_COLOR_TEXT_DIM), 0);
    lv_label_set_text(s_ui.ai_link, latest.ble_connected ? "* iOS LINK" : "* NO LINK");

    lv_label_set_text(s_ui.dashboard_hr, metric_value("hr"));
    lv_label_set_text(s_ui.dashboard_steps, metric_value("steps"));
    char recovery[24];
    std::snprintf(recovery, sizeof(recovery), "%s%%", metric_value("recovery"));
    lv_label_set_text(s_ui.dashboard_recovery, recovery);
    char battery[24];
    std::snprintf(battery, sizeof(battery), "BAT %s", latest.battery_percent >= 0 ? "" : "--%");
    if (latest.battery_percent >= 0) std::snprintf(battery, sizeof(battery), "BAT %d%%", latest.battery_percent);
    lv_label_set_text(s_ui.dashboard_battery, battery);
    char home[32];
    std::snprintf(home, sizeof(home), "VOKRR HOME  *  %d ON", count_devices_on(latest));
    lv_label_set_text(s_ui.dashboard_home, home);

    update_health_rows();
    const uint32_t new_hash = room_state_hash(latest);
    if (new_hash != s_ui.room_hash) rebuild_rooms();

    lv_label_set_text(s_ui.setting_wifi_network, latest.wifi_connected ? latest.wifi_ssid : "--");
    char signal[40];
    std::snprintf(signal, sizeof(signal), latest.wifi_connected ? "%d dBm  *  %s" : "OFFLINE", latest.wifi_rssi, signal_quality(latest.wifi_rssi));
    lv_label_set_text(s_ui.setting_wifi_signal, signal);
    lv_label_set_text(s_ui.setting_wifi_ip, latest.wifi_connected ? latest.ip_address : "--");
    lv_label_set_text(s_ui.setting_bt_status, latest.ble_connected ? "CONNECTED" : "DISCONNECTED");
    lv_label_set_text(s_ui.setting_bt_device, latest.paired_device[0] ? latest.paired_device : "iPhone");
    lv_label_set_text(s_ui.setting_ios_link, latest.ble_connected ? "ACTIVE" : "INACTIVE");
    char ago[32];
    format_ago(latest.last_ios_sync_epoch, ago, sizeof(ago));
    lv_label_set_text(s_ui.setting_ios_sync, ago);

    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    char storage[32];
    std::snprintf(storage, sizeof(storage), "%u MB FLASH", static_cast<unsigned>(flash_size / (1024 * 1024)));
    lv_label_set_text(s_ui.setting_storage, storage);
    char percent[16];
    std::snprintf(percent, sizeof(percent), latest.battery_percent >= 0 ? "%d%%" : "--%%", latest.battery_percent);
    lv_label_set_text(s_ui.setting_battery, percent);
    char health[20];
    std::snprintf(health, sizeof(health), "HEALTH %u%%", latest.battery_health_percent);
    lv_label_set_text(s_ui.setting_battery_health, health);
    lv_obj_set_width(s_ui.setting_battery_bar, latest.battery_percent >= 0 ? latest.battery_percent * 288 / 100 : 1);
    char battery_meta[48];
    std::snprintf(battery_meta, sizeof(battery_meta), "EST. %uH REMAINING  *  %s", latest.battery_estimate_minutes / 60, latest.battery_charging ? "CHARGING" : "ON BATTERY");
    lv_label_set_text(s_ui.setting_battery_meta, battery_meta);
}

void update_timer_cb(lv_timer_t *)
{
    update_time();
    update_live_data();
    if (s_ui.page != 5 && lv_display_get_inactive_time(nullptr) > VOKRR_INACTIVITY_TIMEOUT_MS) {
        set_page(5, true);
    }
}

void motion_timer_cb(lv_timer_t *)
{
    const uint32_t tick = lv_tick_get();
    if (s_ui.page == 5) {
        lv_arc_set_rotation(s_ui.away_arcs[0], static_cast<uint16_t>((tick / 44) % 360));
        lv_arc_set_rotation(s_ui.away_arcs[1], static_cast<uint16_t>(360 - ((tick / 25) % 360)));
        lv_arc_set_rotation(s_ui.away_arcs[2], static_cast<uint16_t>((tick / 67) % 360));
        const float pulse = (std::sin(static_cast<float>(tick) / 414.0f) + 1.0f) * 0.5f;
        lv_obj_set_style_bg_opa(s_ui.away_core, static_cast<lv_opa_t>(static_cast<int>(LV_OPA_20) + pulse * 35), 0);
        lv_obj_set_style_text_opa(s_ui.away_status, static_cast<lv_opa_t>(static_cast<int>(LV_OPA_30) + pulse * 80), 0);
    }
    if (s_ui.listening) {
        for (int index = 0; index < 5; ++index) {
            const float wave = (std::sin(static_cast<float>(tick) / 145.0f + index * 1.15f) + 1.0f) * 0.5f;
            lv_obj_set_height(s_ui.wave_bars[index], 5 + static_cast<int>(wave * 11));
        }
    }
}

void create_dots()
{
    lv_obj_t *container = lv_obj_create(s_ui.root);
    clean_object(container);
    lv_obj_set_size(container, 90, 10);
    lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container, 6, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    for (int index = 0; index < PAGE_COUNT; ++index) {
        s_ui.dots[index] = lv_obj_create(container);
        clean_object(s_ui.dots[index]);
        lv_obj_set_size(s_ui.dots[index], 5, 5);
        lv_obj_set_style_radius(s_ui.dots[index], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_ui.dots[index], LV_OPA_20, 0);
    }
}

}  // namespace

void os_init()
{
    if (s_ui.root) return;
    state_copy(s_ui.snapshot);

    s_ui.root = lv_obj_create(lv_layer_top());
    clean_object(s_ui.root);
    lv_obj_set_size(s_ui.root, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_ui.root, color(WATCH_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_ui.root, LV_OBJ_FLAG_CLICKABLE);

    s_ui.track = lv_obj_create(s_ui.root);
    clean_object(s_ui.track);
    lv_obj_set_size(s_ui.track, SCREEN_W * PAGE_COUNT, SCREEN_H);
    lv_obj_set_pos(s_ui.track, -5 * SCREEN_W, 0);

    create_dashboard();
    create_health();
    create_rooms();
    create_ai();
    create_settings();
    create_away();
    create_dots();
    update_dots();
    update_time();
    update_live_data();

    s_ui.update_timer = lv_timer_create(update_timer_cb, 1000, nullptr);
    s_ui.motion_timer = lv_timer_create(motion_timer_cb, 80, nullptr);
    set_page(5, false);
    register_navigation_input();
}

void os_show_page(uint8_t page, bool animated) { set_page(page, animated); }

void os_on_assistant_text(const char *text)
{
    os_set_listening(false, false);
    add_chat_bubble(false, text);
}

void os_on_chat_exchange(const char *user, const char *assistant)
{
    os_set_listening(false, false);
    add_chat_bubble(true, user);
    add_chat_bubble(false, assistant);
}

void os_set_listening(bool listening, bool processing)
{
    s_ui.listening = listening;
    s_ui.processing = processing;
    if (!s_ui.ai_prompt || !s_ui.ai_mic) return;
    lv_label_set_text(s_ui.ai_prompt, listening ? "" : (processing ? "PROCESSING" : "TAP TO SPEAK"));
    lv_obj_set_style_bg_color(s_ui.ai_mic, color(listening ? WATCH_COLOR_ACCENT : WATCH_COLOR_CHAMPAGNE), 0);
    lv_obj_set_style_bg_opa(s_ui.ai_mic, listening ? static_cast<lv_opa_t>(LV_OPA_20) : OPA_05, 0);
    lv_obj_set_style_border_color(s_ui.ai_mic, color(listening ? WATCH_COLOR_ACCENT : WATCH_COLOR_CHAMPAGNE), 0);
    for (lv_obj_t *bar : s_ui.wave_bars) {
        if (listening) lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
}

void os_set_context(long unix_timestamp, int, const char *)
{
    if (unix_timestamp > 0) {
        s_ui.snapshot.last_ios_sync_epoch = static_cast<uint32_t>(unix_timestamp);
    }
}

}  // namespace vokrr
