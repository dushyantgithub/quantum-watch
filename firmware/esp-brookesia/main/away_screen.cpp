/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Away screen (always-on watch face) for quantum-watch.
 * iOS-style watch face: large hours (gold), large minutes (white),
 * date/day in small text.  24-hour IST format.
 */

#include <ctime>
#include "lvgl.h"
#include "away_screen.h"
#include "bg_image.h"
#include "bsp/esp-bsp.h"

#define AWAY_COLOR_GOLD     0xCCA427
#define AWAY_COLOR_WHITE    0xFFFFFF
#define AWAY_COLOR_GRAY     0x8E8E93
#define AWAY_COLOR_BG       0x000000

#define DOUBLE_TAP_WINDOW_MS 500

static lv_obj_t *s_overlay = nullptr;
static lv_obj_t *s_hour_label = nullptr;
static lv_obj_t *s_min_label = nullptr;
static lv_obj_t *s_date_label = nullptr;
static lv_timer_t *s_update_timer = nullptr;
static bool s_active = false;
static uint32_t s_last_tap_time = 0;

static void update_time_display(void)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%02d", ti.tm_hour);
    lv_label_set_text(s_hour_label, buf);

    lv_snprintf(buf, sizeof(buf), "%02d", ti.tm_min);
    lv_label_set_text(s_min_label, buf);

    static const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                 "Thursday", "Friday", "Saturday"};
    static const char *months[] = {"January", "February", "March", "April",
                                   "May", "June", "July", "August",
                                   "September", "October", "November", "December"};
    char datebuf[48];
    lv_snprintf(datebuf, sizeof(datebuf), "%s, %d %s %d",
                days[ti.tm_wday], ti.tm_mday, months[ti.tm_mon], ti.tm_year + 1900);
    lv_label_set_text(s_date_label, datebuf);
}

static void time_update_cb(lv_timer_t *t)
{
    (void)t;
    if (s_active) { update_time_display(); }
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    uint32_t now = lv_tick_get();
    if (s_last_tap_time != 0 && (now - s_last_tap_time) < DOUBLE_TAP_WINDOW_MS) {
        away_screen_hide();
        s_last_tap_time = 0;
    } else {
        s_last_tap_time = now;
    }
}

void away_screen_init(void)
{
    lv_obj_t *top = lv_layer_top();

    s_overlay = lv_obj_create(top);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(AWAY_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, tap_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bg = lv_image_create(s_overlay);
    lv_image_set_src(bg, &bg_image_dsc);
    lv_obj_set_style_transform_scale_x(bg, 512, 0);
    lv_obj_set_style_transform_scale_y(bg, 512, 0);
    lv_obj_center(bg);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    /*
     * Layout: absolute positioning, vertically centered on screen.
     * Screen is 410x502. Visual block (hours ~110px + gap + minutes ~88px +
     * gap + date 16px ≈ 283px) is centered so its midpoint matches screen center.
     */

    s_hour_label = lv_label_create(s_overlay);
    lv_label_set_text(s_hour_label, "00");
    lv_obj_set_style_text_font(s_hour_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(s_hour_label, lv_color_hex(AWAY_COLOR_GOLD), 0);
    lv_obj_set_style_text_letter_space(s_hour_label, 4, 0);
    lv_obj_set_style_transform_scale_x(s_hour_label, 640, 0);
    lv_obj_set_style_transform_scale_y(s_hour_label, 640, 0);
    lv_obj_align(s_hour_label, LV_ALIGN_CENTER, 0, -86);

    s_min_label = lv_label_create(s_overlay);
    lv_label_set_text(s_min_label, "00");
    lv_obj_set_style_text_font(s_min_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(s_min_label, lv_color_hex(AWAY_COLOR_WHITE), 0);
    lv_obj_set_style_text_letter_space(s_min_label, 4, 0);
    lv_obj_set_style_transform_scale_x(s_min_label, 512, 0);
    lv_obj_set_style_transform_scale_y(s_min_label, 512, 0);
    lv_obj_align(s_min_label, LV_ALIGN_CENTER, 0, 34);

    s_date_label = lv_label_create(s_overlay);
    lv_label_set_text(s_date_label, "---");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(AWAY_COLOR_GRAY), 0);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 134);

    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    s_update_timer = lv_timer_create(time_update_cb, 1000, NULL);
    lv_timer_pause(s_update_timer);
}

void away_screen_show(void)
{
    if (s_active || !s_overlay) { return; }
    s_active = true;
    s_last_tap_time = 0;
    update_time_display();
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_overlay);
    lv_timer_resume(s_update_timer);
    bsp_display_brightness_set(15);
}

void away_screen_hide(void)
{
    if (!s_active || !s_overlay) { return; }
    s_active = false;
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(s_update_timer);
    bsp_display_brightness_set(80);
}

bool away_screen_is_active(void)
{
    return s_active;
}
