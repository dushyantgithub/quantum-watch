/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Away screen (always-on watch face) for quantum-watch.
 * Analog clock with weather and date on black AMOLED background.
 */

#include <ctime>
#include <cstring>
#include <cmath>
#include "lvgl.h"
#include "away_screen.h"
#include "bsp/esp-bsp.h"
#include "watch_theme.h"

#define AWAY_COLOR_GOLD     WATCH_COLOR_ACCENT
#define AWAY_COLOR_WHITE    WATCH_COLOR_TEXT
#define AWAY_COLOR_GRAY     WATCH_COLOR_TEXT_MUTED
#define AWAY_COLOR_DIM_GRAY WATCH_COLOR_SURFACE_ALT
#define AWAY_COLOR_BG       WATCH_COLOR_BG
#define AWAY_COLOR_SEC_RED  WATCH_COLOR_DANGER

#define DOUBLE_TAP_WINDOW_MS 500

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Clock geometry */
#define CLOCK_CX         (LV_HOR_RES / 2)
#define CLOCK_CY         190
#define CLOCK_RADIUS     130
#define MARKER_LEN_HOUR  16
#define MARKER_LEN_MIN   8
#define HAND_HOUR_LEN    65
#define HAND_HOUR_TAIL   12
#define HAND_MIN_LEN     95
#define HAND_MIN_TAIL    16
#define HAND_SEC_LEN     105
#define HAND_SEC_TAIL    22

static lv_obj_t *s_overlay = nullptr;
static lv_obj_t *s_weather_label = nullptr;
static lv_obj_t *s_date_label = nullptr;
static lv_timer_t *s_update_timer = nullptr;
static bool s_active = false;
static uint32_t s_last_tap_time = 0;

/* Clock elements */
static lv_obj_t *s_hour_line = nullptr;
static lv_obj_t *s_min_line = nullptr;
static lv_obj_t *s_sec_line = nullptr;
static lv_obj_t *s_center_dot = nullptr;
static lv_point_precise_t s_hour_pts[2];
static lv_point_precise_t s_min_pts[2];
static lv_point_precise_t s_sec_pts[2];

/* Marker lines (12 hour + 48 minute = 60 total) */
#define NUM_MARKERS 60
static lv_obj_t *s_marker_lines[NUM_MARKERS];
static lv_point_precise_t s_marker_pts[NUM_MARKERS][2];

/* Weather state */
static int s_temp_c = 0;
static char s_location[64] = "";
static bool s_has_weather = false;

static void calc_point(float angle_deg, int radius, lv_coord_t *out_x, lv_coord_t *out_y)
{
    float rad = angle_deg * (float)M_PI / 180.0f;
    *out_x = CLOCK_CX + (lv_coord_t)(radius * sinf(rad));
    *out_y = CLOCK_CY - (lv_coord_t)(radius * cosf(rad));
}

static void set_hand_points(float angle_deg, int length, int tail, lv_point_precise_t *pts)
{
    float rad = angle_deg * (float)M_PI / 180.0f;
    float s = sinf(rad);
    float c = cosf(rad);
    pts[0].x = CLOCK_CX - (lv_coord_t)(tail * s);
    pts[0].y = CLOCK_CY + (lv_coord_t)(tail * c);
    pts[1].x = CLOCK_CX + (lv_coord_t)(length * s);
    pts[1].y = CLOCK_CY - (lv_coord_t)(length * c);
}

static void update_time_display(void)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    /* Hour hand: 30° per hour + 0.5° per minute */
    float hour_angle = (ti.tm_hour % 12) * 30.0f + ti.tm_min * 0.5f;
    set_hand_points(hour_angle, HAND_HOUR_LEN, HAND_HOUR_TAIL, s_hour_pts);
    lv_line_set_points(s_hour_line, s_hour_pts, 2);

    /* Minute hand: 6° per minute + 0.1° per second */
    float min_angle = ti.tm_min * 6.0f + ti.tm_sec * 0.1f;
    set_hand_points(min_angle, HAND_MIN_LEN, HAND_MIN_TAIL, s_min_pts);
    lv_line_set_points(s_min_line, s_min_pts, 2);

    /* Second hand: 6° per second */
    float sec_angle = ti.tm_sec * 6.0f;
    set_hand_points(sec_angle, HAND_SEC_LEN, HAND_SEC_TAIL, s_sec_pts);
    lv_line_set_points(s_sec_line, s_sec_pts, 2);

    /* Weather line */
    if (s_has_weather) {
        char wbuf[96];
        lv_snprintf(wbuf, sizeof(wbuf), "%d\xC2\xB0""C  %s", s_temp_c, s_location);
        lv_label_set_text(s_weather_label, wbuf);
    }

    /* Date line */
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char datebuf[32];
    lv_snprintf(datebuf, sizeof(datebuf), "%s, %d %s",
                days[ti.tm_wday], ti.tm_mday, months[ti.tm_mon]);
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

    /* --- Dial markers --- */
    for (int i = 0; i < NUM_MARKERS; i++) {
        float angle = i * 6.0f; /* 360/60 = 6° per marker */
        bool is_hour = (i % 5 == 0);
        int outer_r = CLOCK_RADIUS;
        int inner_r = CLOCK_RADIUS - (is_hour ? MARKER_LEN_HOUR : MARKER_LEN_MIN);

        calc_point(angle, outer_r, &s_marker_pts[i][1].x, &s_marker_pts[i][1].y);
        calc_point(angle, inner_r, &s_marker_pts[i][0].x, &s_marker_pts[i][0].y);

        s_marker_lines[i] = lv_line_create(s_overlay);
        lv_line_set_points(s_marker_lines[i], s_marker_pts[i], 2);
        lv_obj_set_style_line_color(s_marker_lines[i],
            lv_color_hex(is_hour ? AWAY_COLOR_GOLD : AWAY_COLOR_DIM_GRAY), 0);
        lv_obj_set_style_line_width(s_marker_lines[i], is_hour ? 3 : 1, 0);
        lv_obj_set_style_line_rounded(s_marker_lines[i], true, 0);
    }

    /* --- Clock hands --- */

    /* Hour hand */
    s_hour_line = lv_line_create(s_overlay);
    lv_obj_set_style_line_color(s_hour_line, lv_color_hex(AWAY_COLOR_WHITE), 0);
    lv_obj_set_style_line_width(s_hour_line, 5, 0);
    lv_obj_set_style_line_rounded(s_hour_line, true, 0);

    /* Minute hand */
    s_min_line = lv_line_create(s_overlay);
    lv_obj_set_style_line_color(s_min_line, lv_color_hex(AWAY_COLOR_WHITE), 0);
    lv_obj_set_style_line_width(s_min_line, 3, 0);
    lv_obj_set_style_line_rounded(s_min_line, true, 0);

    /* Second hand */
    s_sec_line = lv_line_create(s_overlay);
    lv_obj_set_style_line_color(s_sec_line, lv_color_hex(AWAY_COLOR_SEC_RED), 0);
    lv_obj_set_style_line_width(s_sec_line, 2, 0);
    lv_obj_set_style_line_rounded(s_sec_line, true, 0);

    /* Center dot */
    s_center_dot = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_center_dot);
    lv_obj_set_size(s_center_dot, 10, 10);
    lv_obj_set_style_radius(s_center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_center_dot, lv_color_hex(AWAY_COLOR_GOLD), 0);
    lv_obj_set_style_bg_opa(s_center_dot, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_center_dot, CLOCK_CX - 5, CLOCK_CY - 5);

    /* --- Info labels below clock --- */

    /* Weather label */
    s_weather_label = lv_label_create(s_overlay);
    lv_label_set_text(s_weather_label, "");
    lv_obj_set_style_text_font(s_weather_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_weather_label, lv_color_hex(AWAY_COLOR_GRAY), 0);
    lv_obj_align(s_weather_label, LV_ALIGN_TOP_MID, 0, 365);

    /* Date label */
    s_date_label = lv_label_create(s_overlay);
    lv_label_set_text(s_date_label, "---");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(AWAY_COLOR_GRAY), 0);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_MID, 0, 390);

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

void away_screen_set_weather(int temp_c, const char *location)
{
    s_temp_c = temp_c;
    s_has_weather = true;
    strncpy(s_location, location, sizeof(s_location) - 1);
    s_location[sizeof(s_location) - 1] = '\0';
}
