/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Settings app for quantum-watch: Display, Sound, WiFi, Bluetooth, Battery.
 * Apple Watch-style navigation: main list -> detail screens.
 */

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "lvgl.h"
#include "driver/i2c_master.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Settings"

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "systems/phone/esp_brookesia_phone.hpp"
#include "systems/phone/assets/esp_brookesia_phone_assets.h"
#include "gui/lvgl/esp_brookesia_lv_lock.hpp"
#include "esp_brookesia_app_settings.hpp"

#ifdef CONFIG_BT_CLASSIC_ENABLED
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_defs.h"
#endif

#define APP_NAME "Settings"
#define NVS_NAMESPACE_SETTINGS "settings"
#define NVS_KEY_BRIGHTNESS "settings_brightness"
#define NVS_KEY_VOLUME "settings_volume"
#define WIFI_PSK_KEY_PREFIX "wifi_psk_"
#define WIFI_PSK_KEY_MAX_LEN 64
#define NVS_KEY_WIFI_ON "wifi_on"
#define NVS_KEY_WIFI_LAST_SSID "wifi_ssid"
#define SLIDER_RANGE 100
#define DEFAULT_BRIGHTNESS 80
#define DEFAULT_VOLUME 60

/* Apple Watch-style colors */
#define COLOR_BG 0x000000
#define COLOR_ROW 0x1c1c1e
#define COLOR_ROW_ICON_BG 0x2c2c2e
#define COLOR_ACCENT_RED 0xff3b30
#define COLOR_ACCENT_GREEN 0x34c759
#define COLOR_TEXT 0xffffff
#define COLOR_TEXT_SECONDARY 0x8e8e93

LV_IMG_DECLARE(esp_brookesia_image_small_app_launcher_default_98_98);

namespace esp_brookesia::apps {

using namespace esp_brookesia::systems;
using namespace esp_brookesia::gui;

SettingsApp *SettingsApp::_instance = nullptr;

enum DetailId {
    DETAIL_NONE = 0,
    DETAIL_DISPLAY,
    DETAIL_SOUND,
    DETAIL_WIFI,
    DETAIL_BLUETOOTH,
    DETAIL_BATTERY,
};

static bool settings_nvs_get_i32(const char *ns, const char *key, int32_t *out_val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) { return false; }
    err = nvs_get_i32(h, key, out_val);
    nvs_close(h);
    return (err == ESP_OK);
}

static bool settings_nvs_set_i32(const char *ns, const char *key, int32_t val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) { return false; }
    err = nvs_set_i32(h, key, val);
    if (err == ESP_OK) { err = nvs_commit(h); }
    nvs_close(h);
    return (err == ESP_OK);
}

static bool settings_nvs_get_str(const char *ns, const char *key, char *buf, size_t *len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) { return false; }
    err = nvs_get_str(h, key, buf, len);
    nvs_close(h);
    return (err == ESP_OK);
}

static bool settings_nvs_set_str(const char *ns, const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) { return false; }
    err = nvs_set_str(h, key, val);
    if (err == ESP_OK) { err = nvs_commit(h); }
    nvs_close(h);
    return (err == ESP_OK);
}

static int get_brightness_from_nvs(void)
{
    int32_t v;
    if (settings_nvs_get_i32(NVS_NAMESPACE_SETTINGS, NVS_KEY_BRIGHTNESS, &v)) {
        return static_cast<int>(std::clamp<int32_t>(v, 0, SLIDER_RANGE));
    }
    return DEFAULT_BRIGHTNESS;
}

static void save_brightness_to_nvs(int val)
{
    settings_nvs_set_i32(NVS_NAMESPACE_SETTINGS, NVS_KEY_BRIGHTNESS, static_cast<int32_t>(val));
}

static int get_volume_from_nvs(void)
{
    int32_t v;
    if (settings_nvs_get_i32(NVS_NAMESPACE_SETTINGS, NVS_KEY_VOLUME, &v)) {
        return static_cast<int>(std::clamp<int32_t>(v, 0, SLIDER_RANGE));
    }
    return DEFAULT_VOLUME;
}

static void save_volume_to_nvs(int val)
{
    settings_nvs_set_i32(NVS_NAMESPACE_SETTINGS, NVS_KEY_VOLUME, static_cast<int32_t>(val));
}

static std::string wifi_psk_key(const char *ssid)
{
    /* NVS keys are limited to 15 chars. Use a djb2 hash of the SSID to keep
     * the key short: "wpk_" (4) + 8 hex digits = 12 chars, well within limit. */
    uint32_t hash = 5381;
    for (const char *p = ssid; *p; p++) {
        hash = ((hash << 5) + hash) + (uint8_t)*p;
    }
    char key[16];
    snprintf(key, sizeof(key), "wpk_%08lx", (unsigned long)hash);
    return std::string(key);
}

static bool get_wifi_psk(const char *ssid, char *psk_buf, size_t psk_len)
{
    std::string key = wifi_psk_key(ssid);
    size_t len = psk_len;
    return settings_nvs_get_str(NVS_NAMESPACE_SETTINGS, key.c_str(), psk_buf, &len);
}

static void save_wifi_psk(const char *ssid, const char *psk)
{
    std::string key = wifi_psk_key(ssid);
    settings_nvs_set_str(NVS_NAMESPACE_SETTINGS, key.c_str(), psk);
}

struct SettingsAppPrivate {
    lv_obj_t *main_list_cont = nullptr;
    lv_obj_t *detail_cont = nullptr;
    DetailId current_detail = DETAIL_NONE;

    lv_obj_t *detail_display = nullptr;
    lv_obj_t *detail_sound = nullptr;
    lv_obj_t *detail_wifi = nullptr;
    lv_obj_t *detail_bt = nullptr;
    lv_obj_t *detail_battery = nullptr;

    lv_obj_t *brightness_slider = nullptr;
    lv_obj_t *brightness_label = nullptr;
    lv_obj_t *volume_slider = nullptr;
    lv_obj_t *volume_label = nullptr;
    lv_obj_t *wifi_switch = nullptr;
    lv_obj_t *wifi_status_label = nullptr;
    lv_obj_t *wifi_list = nullptr;
    lv_obj_t *bt_switch = nullptr;
    lv_obj_t *bt_status_label = nullptr;
    lv_obj_t *bt_list = nullptr;

    lv_obj_t *battery_charge_label = nullptr;
    lv_obj_t *battery_status_label = nullptr;
    lv_timer_t *battery_timer = nullptr;

    lv_obj_t *pwd_modal = nullptr;
    lv_obj_t *pwd_textarea = nullptr;
    lv_obj_t *pwd_keyboard = nullptr;
    std::string wifi_selected_ssid;

    lv_obj_t *wifi_error_popup = nullptr;
    std::string wifi_connected_ssid;

    esp_codec_dev_handle_t audio_handle = nullptr;
    bool audio_inited = false;
    bool wifi_subsys_inited = false;
    bool wifi_started = false;
    bool wifi_connecting = false;
    esp_event_handler_instance_t wifi_scan_done_instance = nullptr;
    esp_event_handler_instance_t wifi_event_instance = nullptr;
    esp_event_handler_instance_t ip_event_instance = nullptr;
};

static SettingsAppPrivate *s_priv = nullptr;
static bool s_wifi_hw_inited = false;
static bool s_ntp_started = false;

static void start_ntp_sync(void)
{
    if (s_ntp_started) { return; }
    s_ntp_started = true;
    setenv("TZ", "IST-5:30", 1);
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    ESP_UTILS_LOGI("NTP sync started (IST timezone)");
}

static void save_wifi_state(bool on, const char *ssid)
{
    settings_nvs_set_i32(NVS_NAMESPACE_SETTINGS, NVS_KEY_WIFI_ON, on ? 1 : 0);
    if (ssid && strlen(ssid) > 0) {
        settings_nvs_set_str(NVS_NAMESPACE_SETTINGS, NVS_KEY_WIFI_LAST_SSID, ssid);
    }
}

static void battery_timer_start(void);
static void battery_timer_stop(void);

static void apply_brightness(int percent)
{
    int p = std::clamp(percent, 0, SLIDER_RANGE);
    esp_err_t err = bsp_display_brightness_set(p);
    if (err == ESP_ERR_INVALID_STATE) {
        bsp_display_brightness_init();
        bsp_display_brightness_set(p);
    }
    if (p > 0) { bsp_display_backlight_on(); }
    else { bsp_display_backlight_off(); }
}

static bool init_audio_once(void)
{
    if (s_priv && s_priv->audio_inited) {
        return (s_priv->audio_handle != nullptr);
    }
    esp_err_t err = bsp_audio_init(NULL);
    if (err != ESP_OK) {
        ESP_UTILS_LOGW("BSP audio init failed: %s", esp_err_to_name(err));
        return false;
    }
    s_priv->audio_handle = bsp_audio_codec_speaker_init();
    if (s_priv->audio_handle == nullptr) {
        ESP_UTILS_LOGW("BSP audio codec init failed");
        return false;
    }
    if (s_priv) { s_priv->audio_inited = true; }
    return true;
}

static void apply_volume(int percent)
{
    if (!init_audio_once() || !s_priv || !s_priv->audio_handle) { return; }
    float vol = (percent / 100.0f) * 100.0f;
    esp_codec_dev_set_out_vol(s_priv->audio_handle, vol);
}

static void update_brightness_label(lv_obj_t *label, int val)
{
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "Brightness: %d%%", val);
    lv_label_set_text(label, buf);
}

static void update_volume_label(lv_obj_t *label, int val)
{
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "Volume: %d%%", val);
    lv_label_set_text(label, buf);
}

static void on_brightness_changed(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    apply_brightness(val);
    save_brightness_to_nvs(val);
    if (s_priv && s_priv->brightness_label) {
        update_brightness_label(s_priv->brightness_label, val);
    }
}

static void on_volume_changed(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    apply_volume(val);
    save_volume_to_nvs(val);
    if (s_priv && s_priv->volume_label) {
        update_volume_label(s_priv->volume_label, val);
    }
}

static void update_status_bar_wifi(bool connected);
static void wifi_connect_impl(const char *ssid, const char *psk);
static bool wifi_ensure_subsys_init(void);
static void wifi_scan_done_cb(void *arg, esp_event_base_t base, int32_t id, void *data);
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
static void show_wifi_error_popup(const char *msg);
static void hide_wifi_error_popup(void);

static void on_wifi_connect_with_psk(void *user_data)
{
    const char *ssid = (const char *)user_data;
    if (!ssid || !s_priv || !s_priv->pwd_textarea) { return; }
    const char *psk = lv_textarea_get_text(s_priv->pwd_textarea);
    if (psk && strlen(psk) > 0) {
        save_wifi_psk(ssid, psk);
        wifi_connect_impl(ssid, psk);
    }
    if (s_priv->pwd_modal) {
        lv_obj_add_flag(s_priv->pwd_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_wifi_pwd_cancel(lv_event_t *e)
{
    (void)e;
    if (s_priv && s_priv->pwd_modal) {
        lv_obj_add_flag(s_priv->pwd_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_wifi_list_btn_del(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    char *ud = (char *)lv_obj_get_user_data(btn);
    if (ud) { free(ud); lv_obj_set_user_data(btn, nullptr); }
}

static void on_wifi_network_clicked(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    if (!ssid) { return; }
    char psk[65] = {0};
    if (get_wifi_psk(ssid, psk, sizeof(psk))) {
        wifi_connect_impl(ssid, psk);
        return;
    }
    if (!s_priv || !s_priv->pwd_modal) { return; }
    s_priv->wifi_selected_ssid = ssid;
    lv_textarea_set_text(s_priv->pwd_textarea, "");
    lv_obj_remove_flag(s_priv->pwd_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_priv->pwd_modal);
}

static void update_status_bar_wifi(bool connected)
{
    auto *app = SettingsApp::requestInstance();
    if (!app) { return; }
    auto *phone = app->getSystem();
    if (!phone) { return; }
    auto *bar = phone->getDisplay().getStatusBar();
    if (!bar) { return; }
    using WS = systems::phone::StatusBar::WifiState;
    bar->setWifiIconState(connected ? WS::SIGNAL_3 : WS::DISCONNECTED);
}

static void wifi_connect_impl(const char *ssid, const char *psk)
{
    if (!wifi_ensure_subsys_init()) { return; }
    if (!s_priv) { return; }

    esp_wifi_disconnect();

    esp_wifi_set_mode(WIFI_MODE_STA);

    if (!s_priv->wifi_started) {
        if (esp_wifi_start() != ESP_OK) {
            ESP_UTILS_LOGW("WiFi start failed in connect_impl");
            return;
        }
        s_priv->wifi_started = true;
    }

    wifi_config_t conf = {};
    strncpy((char *)conf.sta.ssid, ssid, sizeof(conf.sta.ssid) - 1);
    if (psk && strlen(psk) > 0) {
        strncpy((char *)conf.sta.password, psk, sizeof(conf.sta.password) - 1);
    }
    esp_wifi_set_config(WIFI_IF_STA, &conf);

    s_priv->wifi_connecting = true;
    s_priv->wifi_connected_ssid = ssid;
    if (s_priv->wifi_status_label) {
        lv_label_set_text(s_priv->wifi_status_label, "Connecting...");
    }
    esp_wifi_connect();
}

static bool wifi_hw_init(void)
{
    if (s_wifi_hw_inited) { return true; }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_UTILS_LOGW("esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_UTILS_LOGW("esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_UTILS_LOGW("esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_wifi_hw_inited = true;
    return true;
}

static bool wifi_ensure_subsys_init(void)
{
    if (!s_priv) { return false; }
    if (s_priv->wifi_subsys_inited) { return true; }

    if (!wifi_hw_init()) { return false; }

    if (s_priv->wifi_scan_done_instance == nullptr) {
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                           wifi_scan_done_cb, NULL, &s_priv->wifi_scan_done_instance);
    }
    if (s_priv->wifi_event_instance == nullptr) {
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                           wifi_event_handler, NULL, &s_priv->wifi_event_instance);
    }
    if (s_priv->ip_event_instance == nullptr) {
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                           ip_event_handler, NULL, &s_priv->ip_event_instance);
    }

    s_priv->wifi_subsys_inited = true;
    return true;
}

static void wifi_toggle_timer_cb(lv_timer_t *t)
{
    bool on = (bool)(uintptr_t)t->user_data;
    lv_timer_del(t);

    if (!s_priv) { return; }

    if (on) {
        if (!wifi_ensure_subsys_init()) {
            if (s_priv->wifi_status_label) {
                lv_label_set_text(s_priv->wifi_status_label, "WiFi init failed");
            }
            if (s_priv->wifi_switch) {
                lv_obj_remove_state(s_priv->wifi_switch, LV_STATE_CHECKED);
            }
            return;
        }

        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            ESP_UTILS_LOGW("WiFi set mode failed: %s", esp_err_to_name(err));
            if (s_priv->wifi_status_label) {
                lv_label_set_text(s_priv->wifi_status_label, "WiFi start failed");
            }
            if (s_priv->wifi_switch) {
                lv_obj_remove_state(s_priv->wifi_switch, LV_STATE_CHECKED);
            }
            return;
        }

        if (!s_priv->wifi_started) {
            err = esp_wifi_start();
            if (err != ESP_OK) {
                ESP_UTILS_LOGW("WiFi start failed: %s", esp_err_to_name(err));
                if (s_priv->wifi_status_label) {
                    lv_label_set_text(s_priv->wifi_status_label, "WiFi start failed");
                }
                if (s_priv->wifi_switch) {
                    lv_obj_remove_state(s_priv->wifi_switch, LV_STATE_CHECKED);
                }
                return;
            }
            s_priv->wifi_started = true;
        }

        if (s_priv->wifi_status_label) {
            lv_label_set_text(s_priv->wifi_status_label, "Scanning...");
        }
        if (s_priv->wifi_list) {
            lv_obj_remove_flag(s_priv->wifi_list, LV_OBJ_FLAG_HIDDEN);
        }
        esp_wifi_scan_start(NULL, false);
    } else {
        if (s_priv->wifi_started) {
            esp_wifi_stop();
            s_priv->wifi_started = false;
        }
        s_priv->wifi_connected_ssid.clear();
        update_status_bar_wifi(false);
        save_wifi_state(false, nullptr);
        if (s_priv->wifi_status_label) {
            lv_label_set_text(s_priv->wifi_status_label, "Please switch on the WiFi");
        }
        if (s_priv->wifi_list) {
            lv_obj_clean(s_priv->wifi_list);
            lv_obj_add_flag(s_priv->wifi_list, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void on_wifi_switch_changed(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    /* Defer WiFi ops to timer to avoid crash - run outside event handler context */
    lv_timer_t *t = lv_timer_create(wifi_toggle_timer_cb, 50, (void *)(uintptr_t)on);
    lv_timer_set_repeat_count(t, 1);
}

#ifdef CONFIG_BT_CLASSIC_ENABLED
static void on_bt_device_clicked(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    esp_bd_addr_t *addr = (esp_bd_addr_t *)lv_obj_get_user_data(btn);
    if (addr) { esp_bt_gap_connect(*addr); }
}

static void on_bt_switch_changed(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (on) {
        if (s_priv && s_priv->bt_status_label) {
            lv_label_set_text(s_priv->bt_status_label, "Discovering...");
        }
        if (s_priv && s_priv->bt_list) {
            lv_obj_remove_flag(s_priv->bt_list, LV_OBJ_FLAG_HIDDEN);
        }
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 0x0a, 0);
    } else {
        if (s_priv && s_priv->bt_status_label) {
            lv_label_set_text(s_priv->bt_status_label, "Bluetooth off");
        }
        if (s_priv && s_priv->bt_list) {
            lv_obj_add_flag(s_priv->bt_list, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
#endif

static void wifi_scan_done_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != WIFI_EVENT_SCAN_DONE) { return; }
    LvLockGuard gui_guard;
    if (!s_priv || !s_priv->wifi_list) { return; }
    lv_obj_clean(s_priv->wifi_list);
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 32) { ap_count = 32; }
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) { return; }
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    for (uint16_t i = 0; i < ap_count; i++) {
        char ssid[33] = {0};
        memcpy(ssid, ap_list[i].ssid, 32);
        if (!s_priv->wifi_connected_ssid.empty() && s_priv->wifi_connected_ssid == ssid) {
            continue;
        }
        lv_obj_t *row = lv_obj_create(s_priv->wifi_list);
        lv_obj_set_size(row, lv_pct(100), 44);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ROW), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_outline_width(row, 0, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, ssid);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);

        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_TEXT), 0);

        char *ssid_dup = strdup(ssid);
        lv_obj_set_user_data(row, ssid_dup);
        lv_obj_add_event_cb(row, on_wifi_network_clicked, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(row, on_wifi_list_btn_del, LV_EVENT_DELETE, NULL);
    }
    free(ap_list);
    if (s_priv->wifi_status_label) {
        if (!s_priv->wifi_connected_ssid.empty()) {
            char buf[64];
            lv_snprintf(buf, sizeof(buf), "Connected: %s", s_priv->wifi_connected_ssid.c_str());
            lv_label_set_text(s_priv->wifi_status_label, buf);
        } else {
            lv_label_set_text(s_priv->wifi_status_label, "Select a network");
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != WIFI_EVENT) { return; }
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        LvLockGuard gui_guard;
        if (!s_priv) { return; }
        update_status_bar_wifi(false);
        if (s_priv->wifi_connecting) {
            /* User-initiated connection attempt failed */
            s_priv->wifi_connecting = false;
            s_priv->wifi_connected_ssid.clear();
            wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;
            uint8_t reason = evt ? evt->reason : 0;
            bool wrong_password = (reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                   reason == WIFI_REASON_AUTH_FAIL ||
                                   reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                                   reason == WIFI_REASON_MIC_FAILURE);
            if (wrong_password) {
                show_wifi_error_popup("Incorrect password");
            } else {
                show_wifi_error_popup("Connection failed");
            }
            if (s_priv->wifi_status_label) {
                lv_label_set_text(s_priv->wifi_status_label, "Select a network");
            }
        } else if (!s_priv->wifi_connected_ssid.empty()) {
            /* Was connected but dropped -- boot handler will auto-reconnect */
            if (s_priv->wifi_status_label) {
                lv_label_set_text(s_priv->wifi_status_label, "Reconnecting...");
            }
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP) { return; }
    start_ntp_sync();
    LvLockGuard gui_guard;
    if (!s_priv) { return; }
    s_priv->wifi_connecting = false;
    update_status_bar_wifi(true);
    save_wifi_state(true, s_priv->wifi_connected_ssid.c_str());
    if (s_priv->wifi_status_label) {
        char buf[64];
        if (!s_priv->wifi_connected_ssid.empty()) {
            lv_snprintf(buf, sizeof(buf), "Connected: %s", s_priv->wifi_connected_ssid.c_str());
        } else {
            lv_snprintf(buf, sizeof(buf), "Connected");
        }
        lv_label_set_text(s_priv->wifi_status_label, buf);
    }
}

static void show_wifi_error_popup(const char *msg)
{
    if (!s_priv) { return; }
    lv_obj_t *scr = lv_scr_act();
    if (!scr) { return; }

    hide_wifi_error_popup();

    lv_obj_t *popup = lv_obj_create(scr);
    s_priv->wifi_error_popup = popup;
    lv_obj_set_size(popup, 320, 120);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(COLOR_ACCENT_RED), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_radius(popup, 16, 0);
    lv_obj_set_style_pad_all(popup, 16, 0);
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(popup, 12, 0);
    lv_obj_move_foreground(popup);

    lv_obj_t *lbl = lv_label_create(popup);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_ACCENT_RED), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);

    lv_obj_t *ok_btn = lv_btn_create(popup);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(COLOR_ACCENT_RED), 0);
    lv_obj_set_style_radius(ok_btn, 10, 0);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, "OK");
    lv_obj_set_style_text_color(ok_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(ok_lbl);
    lv_obj_add_event_cb(ok_btn, [](lv_event_t *e) {
        (void)e;
        hide_wifi_error_popup();
    }, LV_EVENT_CLICKED, NULL);
}

static void hide_wifi_error_popup(void)
{
    if (s_priv && s_priv->wifi_error_popup) {
        lv_obj_del(s_priv->wifi_error_popup);
        s_priv->wifi_error_popup = nullptr;
    }
}

/* --- Apple Watch style: list item row (2x size) --- */
static lv_obj_t *create_list_item_row(lv_obj_t *parent, const char *icon_symbol, const char *label_text,
                                      lv_event_cb_t click_cb, void *user_data)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 112);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ROW), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_radius(row, 24, 0);
    lv_obj_set_style_pad_all(row, 20, 0);
    lv_obj_set_style_pad_column(row, 24, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon_cont = lv_obj_create(row);
    lv_obj_set_size(icon_cont, 72, 72);
    lv_obj_set_style_bg_color(icon_cont, lv_color_hex(COLOR_ROW_ICON_BG), 0);
    lv_obj_set_style_border_width(icon_cont, 0, 0);
    lv_obj_set_style_outline_width(icon_cont, 0, 0);
    lv_obj_set_style_radius(icon_cont, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(icon_cont, 0, 0);
    lv_obj_clear_flag(icon_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(icon_cont);
    lv_label_set_text(icon, icon_symbol);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT_RED), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_center(icon);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);

    if (click_cb) {
        lv_obj_set_user_data(row, user_data);
        lv_obj_add_event_cb(row, click_cb, LV_EVENT_CLICKED, NULL);
    }
    return row;
}

/* --- Detail header with Back --- */
static lv_obj_t *create_detail_header(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_outline_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 8, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_outline_width(back_btn, 0, 0);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(COLOR_ACCENT_RED), 0);
    lv_obj_center(back_lbl);
    if (back_cb) {
        lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_16, 0);

    return header;
}

/* --- Wi-Fi style toggle row --- */
static lv_obj_t *create_toggle_row(lv_obj_t *parent, const char *label_text, bool checked,
                                   lv_event_cb_t changed_cb, lv_obj_t **out_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 50);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ROW), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);
    if (out_label) { *out_label = lbl; }

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COLOR_ACCENT_GREEN),
                             (lv_style_selector_t)(LV_PART_INDICATOR | LV_STATE_CHECKED));
    if (checked) { lv_obj_add_state(sw, LV_STATE_CHECKED); }
    lv_obj_add_event_cb(sw, changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return sw;
}

static void create_password_modal(lv_obj_t *screen)
{
    if (!s_priv) { return; }
    lv_obj_t *modal = lv_obj_create(screen);
    s_priv->pwd_modal = modal;
    lv_obj_set_size(modal, 380, 340);
    lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_outline_width(modal, 0, 0);
    lv_obj_set_style_radius(modal, 16, 0);
    lv_obj_set_style_pad_all(modal, 12, 0);
    lv_obj_set_style_pad_row(modal, 8, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(modal);
    lv_label_set_text(title, "Enter WiFi password");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);

    lv_obj_t *ta = lv_textarea_create(modal);
    s_priv->pwd_textarea = ta;
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 64);
    lv_obj_set_width(ta, 340);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_outline_width(ta, 0, 0);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);

    /* Button row: Connect + Cancel side by side */
    lv_obj_t *btn_row = lv_obj_create(modal);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_outline_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 12, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_ok = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(COLOR_ACCENT_GREEN), 0);
    lv_obj_set_style_radius(btn_ok, 10, 0);
    lv_obj_set_style_pad_hor(btn_ok, 20, 0);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "Connect");
    lv_obj_set_style_text_color(lbl_ok, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(lbl_ok);
    lv_obj_add_event_cb(btn_ok, [](lv_event_t *e) {
        (void)e;
        if (s_priv && !s_priv->wifi_selected_ssid.empty()) {
            on_wifi_connect_with_psk((void *)s_priv->wifi_selected_ssid.c_str());
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_cancel = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(COLOR_ROW), 0);
    lv_obj_set_style_radius(btn_cancel, 10, 0);
    lv_obj_set_style_pad_hor(btn_cancel, 20, 0);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, on_wifi_pwd_cancel, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(modal);
    s_priv->pwd_keyboard = kb;
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, ta);
}

static void show_main_list(void)
{
    if (!s_priv) { return; }
    battery_timer_stop();
    s_priv->current_detail = DETAIL_NONE;
    if (s_priv->main_list_cont) { lv_obj_remove_flag(s_priv->main_list_cont, LV_OBJ_FLAG_HIDDEN); }
    if (s_priv->detail_cont) { lv_obj_add_flag(s_priv->detail_cont, LV_OBJ_FLAG_HIDDEN); }
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    show_main_list();
}

static void show_detail(DetailId id)
{
    if (!s_priv) { return; }
    s_priv->current_detail = id;
    if (s_priv->main_list_cont) { lv_obj_add_flag(s_priv->main_list_cont, LV_OBJ_FLAG_HIDDEN); }
    if (s_priv->detail_cont) { lv_obj_remove_flag(s_priv->detail_cont, LV_OBJ_FLAG_HIDDEN); }
    if (s_priv->detail_display) { lv_obj_add_flag(s_priv->detail_display, LV_OBJ_FLAG_HIDDEN); }
    if (s_priv->detail_sound) { lv_obj_add_flag(s_priv->detail_sound, LV_OBJ_FLAG_HIDDEN); }
    if (s_priv->detail_wifi) { lv_obj_add_flag(s_priv->detail_wifi, LV_OBJ_FLAG_HIDDEN); }
    if (s_priv->detail_bt) { lv_obj_add_flag(s_priv->detail_bt, LV_OBJ_FLAG_HIDDEN); }
    if (s_priv->detail_battery) {
        lv_obj_add_flag(s_priv->detail_battery, LV_OBJ_FLAG_HIDDEN);
        battery_timer_stop();
    }
    switch (id) {
        case DETAIL_DISPLAY:  if (s_priv->detail_display)  lv_obj_remove_flag(s_priv->detail_display,  LV_OBJ_FLAG_HIDDEN); break;
        case DETAIL_SOUND:   if (s_priv->detail_sound)   lv_obj_remove_flag(s_priv->detail_sound,   LV_OBJ_FLAG_HIDDEN); break;
        case DETAIL_WIFI:    if (s_priv->detail_wifi)   lv_obj_remove_flag(s_priv->detail_wifi,   LV_OBJ_FLAG_HIDDEN); break;
        case DETAIL_BLUETOOTH: if (s_priv->detail_bt)    lv_obj_remove_flag(s_priv->detail_bt,    LV_OBJ_FLAG_HIDDEN); break;
        case DETAIL_BATTERY:
            if (s_priv->detail_battery) {
                lv_obj_remove_flag(s_priv->detail_battery, LV_OBJ_FLAG_HIDDEN);
                battery_timer_start();
            }
            break;
        default: break;
    }
}

/* AXP2101 PMIC (Waveshare ESP32-S3 Touch AMOLED 2.06) - I2C 0x34 */
#define AXP2101_I2C_ADDR      0x34U
#define AXP2101_REG_VBAT_H    0x34U
#define AXP2101_REG_VBAT_L    0x35U
#define AXP2101_REG_STATUS    0x00U
#define AXP2101_CHG_STAT_MASK 0xC0U   /* Bits 7:6 - 00=not charging, 01=charging, 10=done */
#define AXP2101_VBAT_MV_PER_LSB 1U   /* ~1.1 mV per LSB for 12-bit */

struct BatteryInfo {
    int percent;      /* 0-100 or -1 if unknown */
    bool charging;    /* true/false */
    bool available;   /* true if PMIC read succeeded */
};

static bool battery_read_axp2101(BatteryInfo *out)
{
    if (!out) { return false; }
    out->percent = -1;
    out->charging = false;
    out->available = false;

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) { return false; }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = nullptr;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK || !dev) { return false; }

    uint8_t reg_addr = AXP2101_REG_STATUS;
    uint8_t status = 0;
    err = i2c_master_transmit_receive(dev, &reg_addr, 1, &status, 1, 100);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(dev);
        return false;
    }
    out->charging = ((status & AXP2101_CHG_STAT_MASK) == 0x40);

    reg_addr = AXP2101_REG_VBAT_H;
    uint8_t vbat_buf[2] = {0};
    err = i2c_master_transmit_receive(dev, &reg_addr, 1, vbat_buf, 2, 100);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) { return false; }

    /* 12-bit: VbatHigh[3:0] | VbatLow[7:0], 1.1 mV/LSB */
    int raw = ((vbat_buf[0] & 0x0F) << 8) | vbat_buf[1];
    int voltage_mv = raw * AXP2101_VBAT_MV_PER_LSB;
    /* Li-ion: 3000–4200 mV -> 0–100% */
    if (voltage_mv <= 3000) { out->percent = 0; }
    else if (voltage_mv >= 4200) { out->percent = 100; }
    else { out->percent = ((voltage_mv - 3000) * 100) / 1200; }
    out->available = true;
    return true;
}

static void battery_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_priv || s_priv->current_detail != DETAIL_BATTERY) { return; }
    if (!s_priv->battery_charge_label || !s_priv->battery_status_label) { return; }

    BatteryInfo info = {};
    battery_read_axp2101(&info);

    if (info.available) {
        char buf[32];
        lv_snprintf(buf, sizeof(buf), "Charge: %d%%", info.percent);
        lv_label_set_text(s_priv->battery_charge_label, buf);
        lv_obj_set_style_text_color(s_priv->battery_charge_label, lv_color_hex(COLOR_TEXT), 0);
        lv_label_set_text(s_priv->battery_status_label, info.charging ? "Status: Charging" : "Status: Not charging");
        lv_obj_set_style_text_color(s_priv->battery_status_label, lv_color_hex(info.charging ? COLOR_ACCENT_GREEN : COLOR_TEXT_SECONDARY), 0);
    } else {
        lv_label_set_text(s_priv->battery_charge_label, "Charge: --%");
        lv_obj_set_style_text_color(s_priv->battery_charge_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_label_set_text(s_priv->battery_status_label, "Status: Not available");
        lv_obj_set_style_text_color(s_priv->battery_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    }
}

static void battery_timer_start(void)
{
    if (s_priv && s_priv->battery_timer) { return; }
    s_priv->battery_timer = lv_timer_create(battery_timer_cb, 2000, nullptr);
    lv_timer_set_repeat_count(s_priv->battery_timer, -1);
    battery_timer_cb(s_priv->battery_timer);
}

static void battery_timer_stop(void)
{
    if (s_priv && s_priv->battery_timer) {
        lv_timer_del(s_priv->battery_timer);
        s_priv->battery_timer = nullptr;
    }
}

static void on_display_clicked(lv_event_t *e) { (void)e; show_detail(DETAIL_DISPLAY); }
static void on_sound_clicked(lv_event_t *e)   { (void)e; show_detail(DETAIL_SOUND);   }
static void on_wifi_clicked(lv_event_t *e)    { (void)e; show_detail(DETAIL_WIFI);    }
static void on_bt_clicked(lv_event_t *e)     { (void)e; show_detail(DETAIL_BLUETOOTH); }
static void on_battery_clicked(lv_event_t *e) { (void)e; show_detail(DETAIL_BATTERY); }

static void create_main_list(lv_obj_t *screen)
{
    lv_obj_t *cont = lv_obj_create(screen);
    s_priv->main_list_cont = cont;
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_outline_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 20, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(cont, 12, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Header: Settings (time is in status bar) */
    lv_obj_t *header = lv_obj_create(cont);
    lv_obj_set_size(header, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_outline_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_center(title);

    create_list_item_row(cont, LV_SYMBOL_IMAGE, "Display", on_display_clicked, NULL);
    create_list_item_row(cont, LV_SYMBOL_AUDIO, "Sound", on_sound_clicked, NULL);
    create_list_item_row(cont, LV_SYMBOL_WIFI, "Wi-Fi", on_wifi_clicked, NULL);
    create_list_item_row(cont, LV_SYMBOL_BLUETOOTH, "Bluetooth", on_bt_clicked, NULL);
    create_list_item_row(cont, LV_SYMBOL_BATTERY_FULL, "Battery", on_battery_clicked, NULL);
}

static void create_detail_screens(lv_obj_t *screen)
{
    lv_obj_t *detail_cont = lv_obj_create(screen);
    s_priv->detail_cont = detail_cont;
    lv_obj_set_size(detail_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(detail_cont, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(detail_cont, 0, 0);
    lv_obj_set_style_outline_width(detail_cont, 0, 0);
    lv_obj_set_style_pad_all(detail_cont, 20, 0);
    lv_obj_set_flex_flow(detail_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(detail_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(detail_cont, LV_OBJ_FLAG_HIDDEN);

    /* --- Display detail --- */
    lv_obj_t *d_display = lv_obj_create(detail_cont);
    s_priv->detail_display = d_display;
    lv_obj_set_size(d_display, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(d_display, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(d_display, 0, 0);
    lv_obj_set_style_outline_width(d_display, 0, 0);
    lv_obj_set_flex_flow(d_display, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d_display, 12, 0);
    lv_obj_add_flag(d_display, LV_OBJ_FLAG_HIDDEN);
    create_detail_header(d_display, "Display", on_back_clicked);
    int br = get_brightness_from_nvs();
    s_priv->brightness_label = lv_label_create(d_display);
    s_priv->brightness_slider = lv_slider_create(d_display);
    lv_slider_set_range(s_priv->brightness_slider, 0, SLIDER_RANGE);
    lv_slider_set_value(s_priv->brightness_slider, br, LV_ANIM_OFF);
    lv_obj_set_width(s_priv->brightness_slider, lv_pct(95));
    lv_obj_add_event_cb(s_priv->brightness_slider, on_brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    update_brightness_label(s_priv->brightness_label, br);
    apply_brightness(br);

    /* --- Sound detail --- */
    lv_obj_t *d_sound = lv_obj_create(detail_cont);
    s_priv->detail_sound = d_sound;
    lv_obj_set_size(d_sound, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(d_sound, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(d_sound, 0, 0);
    lv_obj_set_style_outline_width(d_sound, 0, 0);
    lv_obj_set_flex_flow(d_sound, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d_sound, 12, 0);
    lv_obj_add_flag(d_sound, LV_OBJ_FLAG_HIDDEN);
    create_detail_header(d_sound, "Sound", on_back_clicked);
    int vol = get_volume_from_nvs();
    s_priv->volume_label = lv_label_create(d_sound);
    s_priv->volume_slider = lv_slider_create(d_sound);
    lv_slider_set_range(s_priv->volume_slider, 0, SLIDER_RANGE);
    lv_slider_set_value(s_priv->volume_slider, vol, LV_ANIM_OFF);
    lv_obj_set_width(s_priv->volume_slider, lv_pct(95));
    lv_obj_add_event_cb(s_priv->volume_slider, on_volume_changed, LV_EVENT_VALUE_CHANGED, NULL);
    update_volume_label(s_priv->volume_label, vol);
    apply_volume(vol);

    /* --- Wi-Fi detail (Apple Watch style like ref image) --- */
    lv_obj_t *d_wifi = lv_obj_create(detail_cont);
    s_priv->detail_wifi = d_wifi;
    lv_obj_set_size(d_wifi, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(d_wifi, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(d_wifi, 0, 0);
    lv_obj_set_style_outline_width(d_wifi, 0, 0);
    lv_obj_set_flex_flow(d_wifi, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d_wifi, 12, 0);
    lv_obj_set_scrollbar_mode(d_wifi, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(d_wifi, LV_DIR_VER);
    lv_obj_add_flag(d_wifi, LV_OBJ_FLAG_HIDDEN);
    create_detail_header(d_wifi, "Wi-Fi", on_back_clicked);

    bool wifi_already_on = s_priv->wifi_started;
    s_priv->wifi_switch = create_toggle_row(d_wifi, "Wi-Fi", wifi_already_on, on_wifi_switch_changed, nullptr);

    lv_obj_t *section = lv_label_create(d_wifi);
    lv_label_set_text(section, "CHOOSE NETWORK");
    lv_obj_set_style_text_color(section, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(section, &lv_font_montserrat_12, 0);

    s_priv->wifi_status_label = lv_label_create(d_wifi);
    if (!s_priv->wifi_connected_ssid.empty()) {
        char buf[64];
        lv_snprintf(buf, sizeof(buf), "Connected: %s", s_priv->wifi_connected_ssid.c_str());
        lv_label_set_text(s_priv->wifi_status_label, buf);
    } else if (wifi_already_on) {
        lv_label_set_text(s_priv->wifi_status_label, "Select a network");
    } else {
        lv_label_set_text(s_priv->wifi_status_label, "Please switch on the WiFi");
    }
    lv_obj_set_style_text_color(s_priv->wifi_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);

    s_priv->wifi_list = lv_obj_create(d_wifi);
    lv_obj_set_size(s_priv->wifi_list, lv_pct(100), 180);
    lv_obj_set_style_bg_opa(s_priv->wifi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_priv->wifi_list, 0, 0);
    lv_obj_set_style_outline_width(s_priv->wifi_list, 0, 0);
    lv_obj_set_flex_flow(s_priv->wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_priv->wifi_list, 4, 0);
    lv_obj_set_scrollbar_mode(s_priv->wifi_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_priv->wifi_list, LV_DIR_VER);
    lv_obj_add_flag(s_priv->wifi_list, LV_OBJ_FLAG_HIDDEN);

    /* --- Bluetooth detail --- */
    lv_obj_t *d_bt = lv_obj_create(detail_cont);
    s_priv->detail_bt = d_bt;
    lv_obj_set_size(d_bt, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(d_bt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(d_bt, 0, 0);
    lv_obj_set_style_outline_width(d_bt, 0, 0);
    lv_obj_set_flex_flow(d_bt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d_bt, 12, 0);
    lv_obj_set_scrollbar_mode(d_bt, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(d_bt, LV_DIR_VER);
    lv_obj_add_flag(d_bt, LV_OBJ_FLAG_HIDDEN);
    create_detail_header(d_bt, "Bluetooth", on_back_clicked);

#ifdef CONFIG_BT_CLASSIC_ENABLED
    s_priv->bt_switch = create_toggle_row(d_bt, "Bluetooth", false, on_bt_switch_changed, nullptr);
    s_priv->bt_status_label = lv_label_create(d_bt);
    lv_label_set_text(s_priv->bt_status_label, "Bluetooth off");
    lv_obj_set_style_text_color(s_priv->bt_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_t *bt_section = lv_label_create(d_bt);
    lv_label_set_text(bt_section, "CHOOSE DEVICE");
    lv_obj_set_style_text_color(bt_section, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(bt_section, &lv_font_montserrat_12, 0);
    s_priv->bt_list = lv_obj_create(d_bt);
    lv_obj_set_size(s_priv->bt_list, lv_pct(100), 120);
    lv_obj_set_style_bg_opa(s_priv->bt_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_priv->bt_list, 0, 0);
    lv_obj_set_style_outline_width(s_priv->bt_list, 0, 0);
    lv_obj_set_flex_flow(s_priv->bt_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_priv->bt_list, 4, 0);
    lv_obj_set_scrollbar_mode(s_priv->bt_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_priv->bt_list, LV_DIR_VER);
    lv_obj_add_flag(s_priv->bt_list, LV_OBJ_FLAG_HIDDEN);
#elif defined(CONFIG_BT_ENABLED)
    lv_obj_t *bt_msg = lv_label_create(d_bt);
    lv_label_set_text(bt_msg, "Bluetooth (BLE) enabled.\nClassic BT not supported on ESP32-S3.");
    lv_obj_set_style_text_color(bt_msg, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
#else
    lv_obj_t *bt_msg = lv_label_create(d_bt);
    lv_label_set_text(bt_msg, "Bluetooth disabled in firmware");
    lv_obj_set_style_text_color(bt_msg, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
#endif

    /* --- Battery detail --- */
    lv_obj_t *d_battery = lv_obj_create(detail_cont);
    s_priv->detail_battery = d_battery;
    lv_obj_set_size(d_battery, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(d_battery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(d_battery, 0, 0);
    lv_obj_set_style_outline_width(d_battery, 0, 0);
    lv_obj_set_flex_flow(d_battery, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d_battery, 12, 0);
    lv_obj_add_flag(d_battery, LV_OBJ_FLAG_HIDDEN);
    create_detail_header(d_battery, "Battery", on_back_clicked);
    s_priv->battery_charge_label = lv_label_create(d_battery);
    lv_label_set_text(s_priv->battery_charge_label, "Charge: --%");
    lv_obj_set_style_text_color(s_priv->battery_charge_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(s_priv->battery_charge_label, &lv_font_montserrat_20, 0);
    s_priv->battery_status_label = lv_label_create(d_battery);
    lv_label_set_text(s_priv->battery_status_label, "Status: --");
    lv_obj_set_style_text_color(s_priv->battery_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(s_priv->battery_status_label, &lv_font_montserrat_18, 0);
}

static void boot_ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP) { return; }
    start_ntp_sync();
    ESP_UTILS_LOGI("Boot WiFi: got IP, NTP sync started");
}

static void boot_wifi_disconnect_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != WIFI_EVENT || id != WIFI_EVENT_STA_DISCONNECTED) { return; }
    wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;
    uint8_t reason = evt ? evt->reason : 0;
    bool auth_fail = (reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                      reason == WIFI_REASON_AUTH_FAIL ||
                      reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                      reason == WIFI_REASON_MIC_FAILURE);
    if (!auth_fail) {
        ESP_UTILS_LOGI("WiFi disconnected (reason=%d), auto-reconnecting...", reason);
        esp_wifi_connect();
    }
}

extern "C" void settings_wifi_boot_connect(void)
{
    setenv("TZ", "IST-5:30", 1);
    tzset();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    int32_t wifi_on = 0;
    if (!settings_nvs_get_i32(NVS_NAMESPACE_SETTINGS, NVS_KEY_WIFI_ON, &wifi_on) || wifi_on == 0) {
        return;
    }

    char ssid[33] = {0};
    size_t ssid_len = sizeof(ssid);
    if (!settings_nvs_get_str(NVS_NAMESPACE_SETTINGS, NVS_KEY_WIFI_LAST_SSID, ssid, &ssid_len) || strlen(ssid) == 0) {
        return;
    }

    char psk[65] = {0};
    std::string key = wifi_psk_key(ssid);
    size_t psk_len = sizeof(psk);
    settings_nvs_get_str(NVS_NAMESPACE_SETTINGS, key.c_str(), psk, &psk_len);

    if (!wifi_hw_init()) {
        ESP_UTILS_LOGW("Boot WiFi: HW init failed");
        return;
    }

    esp_event_handler_instance_t boot_ip_inst = nullptr;
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                       boot_ip_event_handler, NULL, &boot_ip_inst);

    esp_event_handler_instance_t boot_disc_inst = nullptr;
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                       boot_wifi_disconnect_handler, NULL, &boot_disc_inst);

    esp_wifi_set_mode(WIFI_MODE_STA);
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_UTILS_LOGW("Boot WiFi: start failed: %s", esp_err_to_name(err));
        return;
    }

    wifi_config_t conf = {};
    strncpy((char *)conf.sta.ssid, ssid, sizeof(conf.sta.ssid) - 1);
    if (strlen(psk) > 0) {
        strncpy((char *)conf.sta.password, psk, sizeof(conf.sta.password) - 1);
    }
    esp_wifi_set_config(WIFI_IF_STA, &conf);
    esp_wifi_connect();
    ESP_UTILS_LOGI("Boot WiFi: connecting to '%s'", ssid);
}

SettingsApp *SettingsApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new SettingsApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

SettingsApp::SettingsApp(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME, &esp_brookesia_image_small_app_launcher_default_98_98, true, use_status_bar, use_navigation_bar)
{
    s_priv = new SettingsAppPrivate();
}

SettingsApp::~SettingsApp()
{
    delete s_priv;
    s_priv = nullptr;
    _instance = nullptr;
}

bool SettingsApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    lv_obj_t *screen = lv_scr_act();
    if (!screen || !s_priv) { return false; }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Detect if WiFi was already started at boot */
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        s_priv->wifi_subsys_inited = true;
        s_priv->wifi_started = true;
        s_wifi_hw_inited = true;
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_priv->wifi_connected_ssid = (char *)ap.ssid;
        }
    }

    /* Remove white borders, set black background */
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_outline_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    create_main_list(screen);
    create_detail_screens(screen);
    create_password_modal(screen);

    return true;
}

bool SettingsApp::back(void)
{
    ESP_UTILS_LOGD("Back");
    if (s_priv && s_priv->current_detail != DETAIL_NONE) {
        show_main_list();
        return true;
    }
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool SettingsApp::close(void)
{
    ESP_UTILS_LOGD("Close");
    battery_timer_stop();
    hide_wifi_error_popup();
    if (s_priv && s_priv->wifi_scan_done_instance) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, s_priv->wifi_scan_done_instance);
        s_priv->wifi_scan_done_instance = nullptr;
    }
    if (s_priv && s_priv->wifi_event_instance) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_priv->wifi_event_instance);
        s_priv->wifi_event_instance = nullptr;
    }
    if (s_priv && s_priv->ip_event_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_priv->ip_event_instance);
        s_priv->ip_event_instance = nullptr;
    }
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, SettingsApp, APP_NAME, []() {
    return std::shared_ptr<SettingsApp>(SettingsApp::requestInstance(), [](SettingsApp *p) {});
})

} // namespace esp_brookesia::apps
