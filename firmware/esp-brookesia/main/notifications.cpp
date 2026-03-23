/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Notification system for quantum-watch.
 * - Incoming call overlay with beep sound
 * - Notification drawer (swipe down) with missed call history
 */

#include <cstring>
#include <ctime>
#include <cmath>
#include <vector>

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "notifications.h"
#include "away_screen.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Notif"
#include "esp_lib_utils.h"
#include "gui/lvgl/esp_brookesia_lv_lock.hpp"
#include "watch_theme.h"

using namespace esp_brookesia::gui;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Colors ── */
#define N_COLOR_BG          WATCH_COLOR_BG
#define N_COLOR_GOLD        WATCH_COLOR_ACCENT
#define N_COLOR_WHITE       WATCH_COLOR_TEXT
#define N_COLOR_GRAY        WATCH_COLOR_TEXT_MUTED
#define N_COLOR_RED         WATCH_COLOR_DANGER
#define N_COLOR_GREEN       WATCH_COLOR_SUCCESS
#define N_COLOR_DARK_ROW    WATCH_COLOR_SURFACE
#define N_COLOR_OVERLAY_BG  WATCH_COLOR_SURFACE
#define N_COLOR_WA_GREEN    0x25D366
#define N_COLOR_GMAIL_RED   0xEA4335
#define N_COLOR_MAIL_BLUE   0x007AFF
#define N_COLOR_CAL_ORANGE  0xFF9500

/* ── Beep config ── */
#define BEEP_SAMPLE_RATE    16000
#define BEEP_FREQ           1000
#define BEEP_DURATION_MS    300
#define BEEP_PAUSE_MS       700
#define BEEP_AMPLITUDE      12000
#define BEEP_MAX_CYCLES     15   /* Auto-stop after ~15 seconds (15 * 1s cycle) */

/* ── Notification entry ── */
struct NotificationEntry {
    char app[32];
    char title[65];
    char message[129];
    uint8_t category;
    time_t timestamp;
    uint32_t uid;
};

/* ── State ── */
static lv_obj_t *s_call_overlay = nullptr;    /* Incoming call screen */
static lv_obj_t *s_drawer = nullptr;          /* Notification drawer */
static lv_obj_t *s_drawer_list = nullptr;     /* List inside drawer */
static lv_obj_t *s_badge = nullptr;           /* Unread badge at top-right */
static lv_obj_t *s_notch = nullptr;           /* Visual pull-down notch */

/* Swipe-from-top tracking (indev-level, uses PRESSED/RELEASED to avoid
   scroll-consuming gesture events on the Phone framework's scrollable areas) */
static lv_coord_t s_press_start_y = -1;
static lv_coord_t s_press_start_x = -1;

static bool s_call_active = false;
static bool s_drawer_open = false;
static TaskHandle_t s_beep_task = nullptr;
static std::vector<NotificationEntry> s_notifications;
/* Protects s_notifications — accessed from BLE callbacks (ANCS) and LVGL task */
static SemaphoreHandle_t s_notif_mutex = nullptr;

/* ── Beep sound (pre-initialized at boot to avoid I2C/codec init during calls) ── */
static esp_codec_dev_handle_t s_spk = nullptr;
static int16_t *s_tone = nullptr;
static int16_t *s_silence = nullptr;
static int s_tone_bytes = 0;
static int s_silence_bytes = 0;

static void beep_preinit(void)
{
    s_spk = bsp_audio_codec_speaker_init();
    if (!s_spk) {
        ESP_UTILS_LOGE("Failed to init speaker codec — beep disabled");
        return;
    }

    /* Pre-generate tone and silence buffers */
    const int beep_samples = (BEEP_SAMPLE_RATE * BEEP_DURATION_MS) / 1000;
    s_tone_bytes = beep_samples * sizeof(int16_t);
    s_tone = (int16_t *)malloc(s_tone_bytes);
    if (s_tone) {
        for (int i = 0; i < beep_samples; i++) {
            float t = (float)i / BEEP_SAMPLE_RATE;
            s_tone[i] = (int16_t)(BEEP_AMPLITUDE * sinf(2.0f * (float)M_PI * BEEP_FREQ * t));
        }
    }

    const int pause_samples = (BEEP_SAMPLE_RATE * BEEP_PAUSE_MS) / 1000;
    s_silence_bytes = pause_samples * sizeof(int16_t);
    s_silence = (int16_t *)calloc(pause_samples, sizeof(int16_t));

    ESP_UTILS_LOGI("Beep pre-init done (spk=%p, tone=%p)", (void *)s_spk, (void *)s_tone);
}

static void auto_dismiss_call(void);

static void beep_task_fn(void *arg)
{
    (void)arg;

    /* Codec + buffers already initialized at boot */
    if (!s_spk || !s_tone) {
        ESP_UTILS_LOGE("Beep not available (pre-init failed)");
        s_beep_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = 1;
    fs.sample_rate = BEEP_SAMPLE_RATE;
    esp_codec_dev_open(s_spk, &fs);
    esp_codec_dev_set_out_vol(s_spk, 80);

    int cycles = 0;
    while (1) {
        esp_codec_dev_write(s_spk, s_tone, s_tone_bytes);
        if (s_silence) {
            esp_codec_dev_write(s_spk, s_silence, s_silence_bytes);
        } else {
            vTaskDelay(pdMS_TO_TICKS(BEEP_PAUSE_MS));
        }

        cycles++;
        if (ulTaskNotifyTake(pdTRUE, 0) || cycles >= BEEP_MAX_CYCLES) {
            break;
        }
    }

    esp_codec_dev_close(s_spk);
    s_beep_task = nullptr;

    /* If we timed out (not explicitly stopped), auto-dismiss the call overlay */
    if (cycles >= BEEP_MAX_CYCLES && s_call_active) {
        auto_dismiss_call();
    }

    vTaskDelete(nullptr);
}

static void auto_dismiss_call(void)
{
    ESP_UTILS_LOGI("Beep timed out — auto-dismissing call overlay");
    s_call_active = false;
    LvLockGuard gui_guard;
    lv_obj_add_flag(s_call_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void start_beep(void)
{
    if (s_beep_task) return;
    xTaskCreatePinnedToCore(beep_task_fn, "beep", 4096, nullptr, 3, &s_beep_task, 0);
}

static void stop_beep(void)
{
    if (s_beep_task) {
        /* Only notify — do not clear s_beep_task here. The beep task clears it after
         * esp_codec_dev_close(); clearing early allowed a second start_beep() while the
         * first task still held the codec, leading to duplicate tasks and eventual lockups. */
        xTaskNotifyGive(s_beep_task);
    }
}

/* ── Call overlay ── */

static void call_dismiss_cb(lv_event_t *e)
{
    (void)e;
    /* User tapped dismiss — treat as acknowledged */
    if (s_call_active) {
        s_call_active = false;
        stop_beep();
        lv_obj_add_flag(s_call_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_call_overlay(void)
{
    lv_obj_t *top = lv_layer_top();

    s_call_overlay = lv_obj_create(top);
    lv_obj_remove_style_all(s_call_overlay);
    lv_obj_set_size(s_call_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(s_call_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_call_overlay, lv_color_hex(N_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_call_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_call_overlay, 0, 0);
    lv_obj_set_style_radius(s_call_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_call_overlay, 0, 0);
    lv_obj_clear_flag(s_call_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Phone icon (using text) */
    lv_obj_t *icon = lv_label_create(s_call_overlay);
    lv_label_set_text(icon, LV_SYMBOL_CALL);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(N_COLOR_GREEN), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 120);

    /* "Incoming Call" label */
    lv_obj_t *title = lv_label_create(s_call_overlay);
    lv_label_set_text(title, "Incoming Call");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(N_COLOR_WHITE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 200);

    /* "iPhone" subtitle */
    lv_obj_t *subtitle = lv_label_create(s_call_overlay);
    lv_label_set_text(subtitle, "iPhone");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(N_COLOR_GRAY), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 240);

    /* Dismiss button */
    lv_obj_t *dismiss_btn = lv_obj_create(s_call_overlay);
    lv_obj_remove_style_all(dismiss_btn);
    lv_obj_set_size(dismiss_btn, 80, 80);
    lv_obj_set_style_radius(dismiss_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dismiss_btn, lv_color_hex(N_COLOR_RED), 0);
    lv_obj_set_style_bg_opa(dismiss_btn, LV_OPA_COVER, 0);
    lv_obj_align(dismiss_btn, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_add_flag(dismiss_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dismiss_btn, call_dismiss_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *dismiss_icon = lv_label_create(dismiss_btn);
    lv_label_set_text(dismiss_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(dismiss_icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(dismiss_icon, lv_color_hex(N_COLOR_WHITE), 0);
    lv_obj_center(dismiss_icon);

    lv_obj_add_flag(s_call_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* ── Notification drawer ── */

static void format_time(time_t ts, char *buf, size_t len)
{
    struct tm ti;
    localtime_r(&ts, &ti);
    int hour12 = ti.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char *ampm = (ti.tm_hour >= 12) ? "PM" : "AM";

    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    lv_snprintf(buf, len, "%d %s, %d:%02d %s",
                ti.tm_mday, months[ti.tm_mon], hour12, ti.tm_min, ampm);
}

/* Lock/unlock helpers for the notification list mutex */
static inline bool notif_lock(void) {
    return s_notif_mutex && xSemaphoreTake(s_notif_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}
static inline void notif_unlock(void) {
    if (s_notif_mutex) xSemaphoreGive(s_notif_mutex);
}

static void update_badge(void)
{
    if (!s_badge) return;
    int count = 0;
    if (notif_lock()) {
        count = (int)s_notifications.size();
        notif_unlock();
    }
    if (count == 0) {
        lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", count);
        lv_label_set_text(lv_obj_get_child(s_badge, 0), buf);
        lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clear_all_cb(lv_event_t *e);

/* Get icon symbol and color for a given app name */
static void get_app_icon(const char *app, const char **symbol, uint32_t *color)
{
    if (strcmp(app, "Phone") == 0) {
        *symbol = LV_SYMBOL_CALL;
        *color = N_COLOR_GREEN;
    } else if (strcmp(app, "WhatsApp") == 0) {
        *symbol = "W";
        *color = N_COLOR_WA_GREEN;
    } else if (strcmp(app, "Gmail") == 0) {
        *symbol = "G";
        *color = N_COLOR_GMAIL_RED;
    } else if (strcmp(app, "Mail") == 0) {
        *symbol = LV_SYMBOL_ENVELOPE;
        *color = N_COLOR_MAIL_BLUE;
    } else if (strcmp(app, "Calendar") == 0) {
        *symbol = LV_SYMBOL_LIST;
        *color = N_COLOR_CAL_ORANGE;
    } else {
        *symbol = LV_SYMBOL_BELL;
        *color = N_COLOR_GRAY;
    }
}

static void rebuild_drawer_list(void)
{
    if (!s_drawer_list) return;
    if (!notif_lock()) return;

    /* Clear existing children */
    lv_obj_clean(s_drawer_list);

    if (s_notifications.empty()) {
        notif_unlock();
        lv_obj_t *empty = lv_label_create(s_drawer_list);
        lv_label_set_text(empty, "No notifications");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(N_COLOR_GRAY), 0);
        lv_obj_set_width(empty, LV_HOR_RES - 40);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(empty, 60, 0);
        return;
    }

    /* Clear All button at top of list */
    lv_obj_t *clear_btn = lv_label_create(s_drawer_list);
    lv_label_set_text(clear_btn, "Clear All");
    lv_obj_set_style_text_font(clear_btn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(clear_btn, lv_color_hex(N_COLOR_GOLD), 0);
    lv_obj_set_width(clear_btn, LV_HOR_RES - 40);
    lv_obj_set_style_text_align(clear_btn, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(clear_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clear_btn, clear_all_cb, LV_EVENT_CLICKED, nullptr);

    /* Show notifications newest first */
    for (int i = (int)s_notifications.size() - 1; i >= 0; i--) {
        const NotificationEntry &n = s_notifications[i];

        const char *icon_sym = LV_SYMBOL_BELL;
        uint32_t icon_color = N_COLOR_GRAY;
        get_app_icon(n.app, &icon_sym, &icon_color);

        lv_obj_t *row = lv_obj_create(s_drawer_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_HOR_RES - 40, 64);
        lv_obj_set_style_bg_color(row, lv_color_hex(N_COLOR_DARK_ROW), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_set_style_pad_right(row, 10, 0);

        /* App icon circle on the left */
        lv_obj_t *icon_circle = lv_obj_create(row);
        lv_obj_remove_style_all(icon_circle);
        lv_obj_set_size(icon_circle, 32, 32);
        lv_obj_set_style_radius(icon_circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(icon_circle, lv_color_hex(icon_color), 0);
        lv_obj_set_style_bg_opa(icon_circle, LV_OPA_COVER, 0);
        lv_obj_align(icon_circle, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *icon_label = lv_label_create(icon_circle);
        lv_label_set_text(icon_label, icon_sym);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(icon_label, lv_color_hex(N_COLOR_WHITE), 0);
        lv_obj_center(icon_label);

        /* Text content offset to the right of the icon */
        int text_x = 40;

        /* App name + timestamp on line 1 */
        lv_obj_t *app_label = lv_label_create(row);
        lv_label_set_text(app_label, n.app);
        lv_obj_set_style_text_font(app_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(app_label, lv_color_hex(N_COLOR_GOLD), 0);
        lv_obj_align(app_label, LV_ALIGN_TOP_LEFT, text_x, 8);

        char timebuf[32];
        format_time(n.timestamp, timebuf, sizeof(timebuf));
        lv_obj_t *ts_label = lv_label_create(row);
        lv_label_set_text(ts_label, timebuf);
        lv_obj_set_style_text_font(ts_label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ts_label, lv_color_hex(N_COLOR_GRAY), 0);
        lv_obj_align(ts_label, LV_ALIGN_TOP_RIGHT, 0, 8);

        /* Title + message on line 2 */
        char body[200];
        if (n.title[0] && n.message[0]) {
            lv_snprintf(body, sizeof(body), "%s: %s", n.title, n.message);
        } else if (n.title[0]) {
            lv_snprintf(body, sizeof(body), "%s", n.title);
        } else if (n.message[0]) {
            lv_snprintf(body, sizeof(body), "%s", n.message);
        } else {
            lv_snprintf(body, sizeof(body), "New notification");
        }

        lv_obj_t *body_label = lv_label_create(row);
        lv_label_set_text(body_label, body);
        lv_obj_set_style_text_font(body_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(body_label, lv_color_hex(N_COLOR_WHITE), 0);
        lv_obj_set_width(body_label, LV_HOR_RES - 100);
        lv_label_set_long_mode(body_label, LV_LABEL_LONG_DOT);
        lv_obj_align(body_label, LV_ALIGN_TOP_LEFT, text_x, 30);
    }
    notif_unlock();
}

static void drawer_close_cb(lv_event_t *e)
{
    (void)e;
    if (s_drawer_open) {
        s_drawer_open = false;
        lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clear_all_cb(lv_event_t *e)
{
    (void)e;
    if (notif_lock()) {
        s_notifications.clear();
        notif_unlock();
    }
    rebuild_drawer_list();
    update_badge();
}

static void create_drawer(void)
{
    lv_obj_t *top = lv_layer_top();

    s_drawer = lv_obj_create(top);
    lv_obj_remove_style_all(s_drawer);
    lv_obj_set_size(s_drawer, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(s_drawer, 0, 0);
    lv_obj_set_style_bg_color(s_drawer, lv_color_hex(N_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_drawer, 0, 0);
    lv_obj_set_style_radius(s_drawer, 0, 0);
    lv_obj_set_style_pad_all(s_drawer, 0, 0);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);

    /* Handle bar at top (visual indicator) */
    lv_obj_t *handle = lv_obj_create(s_drawer);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, 40, 4);
    lv_obj_set_style_radius(handle, 2, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(N_COLOR_GRAY), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 12);

    /* Title */
    lv_obj_t *title = lv_label_create(s_drawer);
    lv_label_set_text(title, "Notifications");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(N_COLOR_WHITE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    /* Scrollable list area */
    s_drawer_list = lv_obj_create(s_drawer);
    lv_obj_remove_style_all(s_drawer_list);
    lv_obj_set_size(s_drawer_list, LV_HOR_RES, LV_VER_RES - 90);
    lv_obj_set_pos(s_drawer_list, 0, 85);
    lv_obj_set_style_pad_left(s_drawer_list, 20, 0);
    lv_obj_set_style_pad_right(s_drawer_list, 20, 0);
    lv_obj_set_style_pad_top(s_drawer_list, 8, 0);
    lv_obj_set_style_pad_row(s_drawer_list, 8, 0);
    lv_obj_set_flex_flow(s_drawer_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_drawer_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_drawer_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_drawer_list, LV_DIR_VER);
    lv_obj_set_style_bg_opa(s_drawer_list, LV_OPA_TRANSP, 0);

    /* Close button at bottom */
    lv_obj_t *close_btn = lv_obj_create(s_drawer);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 50, 50);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(N_COLOR_DARK_ROW), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_btn, drawer_close_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, LV_SYMBOL_UP);
    lv_obj_set_style_text_font(close_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(close_icon, lv_color_hex(N_COLOR_WHITE), 0);
    lv_obj_center(close_icon);

    lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
}

/* ── Indev-level swipe-from-top detection ──
 *
 * We use PRESSED + RELEASED (not GESTURE) because LV_EVENT_GESTURE
 * is suppressed when the underlying LVGL object handles scrolling —
 * the esp-brookesia Phone's main screen is scrollable, so gestures
 * starting on it are consumed as scroll and never fire GESTURE. */

#define SWIPE_START_ZONE   50   /* Must start within top 50px */
#define SWIPE_MIN_DY       80   /* Minimum downward travel */

static void indev_pressed_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_press_start_y = p.y;
    s_press_start_x = p.x;
}

static void indev_released_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    /* Only consider touches that started in the top zone */
    if (s_press_start_y < 0 || s_press_start_y > SWIPE_START_ZONE) {
        s_press_start_y = -1;
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_coord_t dy = p.y - s_press_start_y;
    lv_coord_t dx = p.x - s_press_start_x;
    if (dx < 0) dx = -dx;

    s_press_start_y = -1;

    /* Require a mostly-vertical downward swipe */
    if (dy < SWIPE_MIN_DY || dx > dy) return;
    if (s_drawer_open || s_call_active || away_screen_is_active()) return;

    ESP_UTILS_LOGI("Swipe-down from top detected (dy=%d, dx=%d)", (int)dy, (int)dx);
    s_drawer_open = true;
    rebuild_drawer_list();
    lv_obj_remove_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_drawer);
}

static void setup_swipe_detection(void)
{
    /* Register on the touch indev directly — fires for ALL touches,
       independent of which LVGL object is under the finger.
       Uses PRESSED/RELEASED to bypass scroll-consuming gesture events. */
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_add_event_cb(indev, indev_pressed_cb, LV_EVENT_PRESSED, nullptr);
            lv_indev_add_event_cb(indev, indev_released_cb, LV_EVENT_RELEASED, nullptr);
            ESP_UTILS_LOGI("Swipe detection registered on indev %p", (void *)indev);
            break;
        }
        indev = lv_indev_get_next(indev);
    }

    /* Visual notch at top-center so user knows to pull down */
    lv_obj_t *top = lv_layer_top();

    s_notch = lv_obj_create(top);
    lv_obj_remove_style_all(s_notch);
    lv_obj_set_size(s_notch, 36, 4);
    lv_obj_set_style_radius(s_notch, 2, 0);
    lv_obj_set_style_bg_color(s_notch, lv_color_hex(N_COLOR_GRAY), 0);
    lv_obj_set_style_bg_opa(s_notch, LV_OPA_70, 0);
    lv_obj_align(s_notch, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_clear_flag(s_notch, LV_OBJ_FLAG_CLICKABLE);

    /* Unread badge (small red dot with count) */
    s_badge = lv_obj_create(top);
    lv_obj_remove_style_all(s_badge);
    lv_obj_set_size(s_badge, 22, 22);
    lv_obj_set_style_radius(s_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_badge, lv_color_hex(N_COLOR_RED), 0);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_badge, LV_HOR_RES - 30, 6);

    lv_obj_t *badge_label = lv_label_create(s_badge);
    lv_label_set_text(badge_label, "0");
    lv_obj_set_style_text_font(badge_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(badge_label, lv_color_hex(N_COLOR_WHITE), 0);
    lv_obj_center(badge_label);

    lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
}

/* ── Public API ── */

void notifications_init(void)
{
    /* Create mutex for thread-safe access to s_notifications */
    if (!s_notif_mutex) {
        s_notif_mutex = xSemaphoreCreateMutex();
    }

    /* Pre-init audio codec + tone buffers BEFORE taking the LVGL lock
       (codec init uses I2C which may contend with touch controller) */
    beep_preinit();

    LvLockGuard gui_guard;
    create_call_overlay();
    create_drawer();
    setup_swipe_detection();
}

void notifications_call_incoming(void)
{
    ESP_UTILS_LOGI("Call incoming (active=%d)", s_call_active);
    if (s_call_active) return;
    s_call_active = true;

    /* Show call overlay — keep lock scope minimal so LVGL task can
       start rendering the overlay ASAP without waiting for beep init */
    {
        LvLockGuard gui_guard;

        /* Dismiss away screen if active */
        if (away_screen_is_active()) {
            away_screen_hide();
        }

        lv_obj_remove_flag(s_call_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_call_overlay);
    }

    /* Start beep AFTER releasing the LVGL lock — xTaskCreate + audio
       codec init are heavy; doing them inside the lock starves the
       LVGL render task and causes visible tearing */
    start_beep();
}

void notifications_call_ended(void)
{
    ESP_UTILS_LOGI("Call ended (active=%d)", s_call_active);
    stop_beep();
    if (!s_call_active) return;
    s_call_active = false;

    LvLockGuard gui_guard;
    lv_obj_add_flag(s_call_overlay, LV_OBJ_FLAG_HIDDEN);
}

void notifications_call_missed(void)
{
    ESP_UTILS_LOGI("Call missed (active=%d)", s_call_active);
    stop_beep();

    /* Add to notifications as missed call */
    NotificationEntry entry = {};
    strncpy(entry.app, "Phone", sizeof(entry.app) - 1);
    strncpy(entry.title, "Missed Call", sizeof(entry.title) - 1);
    entry.message[0] = '\0';
    entry.category = 1; /* IncomingCall category */
    time(&entry.timestamp);
    entry.uid = 0;

    if (notif_lock()) {
        s_notifications.push_back(entry);
        if (s_notifications.size() > 50) {
            s_notifications.erase(s_notifications.begin());
        }
        notif_unlock();
    }

    if (s_call_active) {
        s_call_active = false;
        LvLockGuard gui_guard;
        lv_obj_add_flag(s_call_overlay, LV_OBJ_FLAG_HIDDEN);
        update_badge();
    } else {
        LvLockGuard gui_guard;
        update_badge();
    }
}

bool notifications_drawer_is_open(void)
{
    return s_drawer_open;
}

void notifications_add(const char *app, const char *title,
                       const char *message, uint8_t category,
                       uint32_t uid)
{
    NotificationEntry entry = {};
    if (app) strncpy(entry.app, app, sizeof(entry.app) - 1);
    if (title) strncpy(entry.title, title, sizeof(entry.title) - 1);
    if (message) strncpy(entry.message, message, sizeof(entry.message) - 1);
    entry.category = category;
    entry.uid = uid;
    time(&entry.timestamp);

    if (!notif_lock()) return;
    s_notifications.push_back(entry);
    if (s_notifications.size() > 50) {
        s_notifications.erase(s_notifications.begin());
    }
    int total = (int)s_notifications.size();
    notif_unlock();

    ESP_UTILS_LOGI("Notification added: app=%s title=%s uid=%lu (total=%d)",
                   entry.app, entry.title, (unsigned long)uid, total);

    LvLockGuard gui_guard;
    update_badge();
    if (s_drawer_open) {
        rebuild_drawer_list();
    }
}

void notifications_remove(uint32_t uid)
{
    bool found = false;
    if (notif_lock()) {
        for (auto it = s_notifications.begin(); it != s_notifications.end(); ++it) {
            if (it->uid == uid) {
                ESP_UTILS_LOGI("Notification removed: uid=%lu", (unsigned long)uid);
                s_notifications.erase(it);
                found = true;
                break;
            }
        }
        notif_unlock();
    }
    if (found) {
        LvLockGuard gui_guard;
        update_badge();
        if (s_drawer_open) {
            rebuild_drawer_list();
        }
    }
}
