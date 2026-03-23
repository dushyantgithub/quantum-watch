/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Voice Assistant app for quantum-watch.
 * Captures mic audio, compresses to IMA-ADPCM, streams over BLE to iPhone companion.
 * Receives AI response text back over BLE and displays it.
 */

#include <cstring>
#include <cstdlib>
#include <string>
#include <sys/time.h>

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_codec_dev.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_bt_defs.h"
#include "esp_gattc_api.h"
#include "ancs_client.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:VoiceAssist"

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "systems/phone/esp_brookesia_phone.hpp"
#include "systems/phone/assets/esp_brookesia_phone_assets.h"
#include "gui/lvgl/esp_brookesia_lv_lock.hpp"
#include "voice_assistant.h"
#include "away_screen.h"
#include "notifications.h"
#include "health_app.h"
#include "watch_theme.h"

/* ── App name ── */
#define APP_NAME "Voice Assistant"

/* ── BLE UUIDs ── */
#define VA_SERVICE_UUID        0xAA00
#define VA_CHAR_AUDIO_UUID     0xAA01
#define VA_CHAR_TEXT_UUID      0xAA02
#define VA_CHAR_CONTEXT_UUID   0xAA03
#define VA_CHAR_NOTIF_UUID     0xAA04
#define VA_CHAR_HEALTH_REQ_UUID 0xAA05
#define VA_CHAR_HEALTH_DATA_UUID 0xAA06

/* ── Audio config ── */
#define AUDIO_SAMPLE_RATE      16000
#define AUDIO_BITS_PER_SAMPLE  16
#define AUDIO_CHANNELS         1
#define PCM_CHUNK_SAMPLES      480    /* 30ms at 16kHz */
#define PCM_CHUNK_BYTES        (PCM_CHUNK_SAMPLES * 2)
#define ADPCM_CHUNK_BYTES      238    /* Compressed size per BLE packet payload */
#define BLE_PACKET_SIZE        240    /* 2-byte seq + 238-byte ADPCM */
#define END_OF_AUDIO_MARKER    0xFFFFFFFF

/* ── UI colors ── */
#define COLOR_BG               WATCH_COLOR_BG
#define COLOR_GOLD             WATCH_COLOR_ACCENT
#define COLOR_TEXT             WATCH_COLOR_TEXT
#define COLOR_TEXT_SECONDARY   WATCH_COLOR_TEXT_MUTED
#define COLOR_RED              WATCH_COLOR_DANGER
#define COLOR_GREEN            WATCH_COLOR_SUCCESS
#define COLOR_GRAY             WATCH_COLOR_TEXT_DIM
#define COLOR_DARK_ROW         WATCH_COLOR_SURFACE

/* ── BLE config ── */
#define GATTS_APP_ID           0
#define DEVICE_NAME            "QuantumWatch"
#define ADV_CONFIG_FLAG        (1 << 0)
#define SCAN_RSP_CONFIG_FLAG   (1 << 1)

LV_IMG_DECLARE(quantum_watch_voice_assistant_icon_98_98);

namespace esp_brookesia::apps {

using namespace esp_brookesia::systems;
using namespace esp_brookesia::gui;

VoiceAssistantApp *VoiceAssistantApp::_instance = nullptr;

/* ── App states ── */
enum VaState {
    VA_STATE_IDLE,
    VA_STATE_LISTENING,
    VA_STATE_PROCESSING,
    VA_STATE_RESPONSE,
};

/* ── IMA-ADPCM encoder state ── */
struct AdpcmState {
    int16_t predicted;
    int8_t  index;
};

/* ── Global BLE state (persists across app open/close) ── */
static struct {
    uint16_t gatts_if = ESP_GATT_IF_NONE;
    uint16_t conn_id = 0;
    bool     connected = false;
    bool     inited = false;
    bool     audio_notify_enabled = false;
    bool     health_request_notify_enabled = false;
    uint16_t audio_attr_handle = 0;
    uint16_t text_attr_handle = 0;
    uint16_t context_attr_handle = 0;
    uint16_t notif_attr_handle = 0;
    uint16_t health_request_attr_handle = 0;
    uint16_t health_data_attr_handle = 0;
    uint16_t service_handle = 0;
    uint16_t mtu = 23;
} g_ble;

/* ── BLE keepalive ──
 * iOS suspends background apps between BLE events. CXCallObserver callbacks
 * (for phone call detection) are queued but NOT delivered until the app wakes.
 * Sending a periodic BLE notification wakes the iOS app so it can process
 * pending call events and forward them to the watch. */
#define BLE_KEEPALIVE_INTERVAL_MS  3000
static TimerHandle_t s_keepalive_timer = nullptr;
static const uint8_t KEEPALIVE_BYTE = 0xFE;  /* Distinguishable from audio/EOA */

static void keepalive_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!g_ble.connected || !g_ble.audio_notify_enabled) return;
    uint8_t ping = KEEPALIVE_BYTE;
    esp_ble_gatts_send_indicate(g_ble.gatts_if, g_ble.conn_id,
                                g_ble.audio_attr_handle, 1, &ping, false);
}

static void keepalive_start(void)
{
    if (!s_keepalive_timer) {
        s_keepalive_timer = xTimerCreate("ble_ka", pdMS_TO_TICKS(BLE_KEEPALIVE_INTERVAL_MS),
                                          pdTRUE, nullptr, keepalive_timer_cb);
    }
    if (s_keepalive_timer) {
        xTimerStart(s_keepalive_timer, 0);
    }
}

static void keepalive_stop(void)
{
    if (s_keepalive_timer) {
        xTimerStop(s_keepalive_timer, 0);
    }
}

/* ── Private data (per app instance) ── */
struct VaPrivate {
    VaState state = VA_STATE_IDLE;

    /* Audio */
    esp_codec_dev_handle_t mic_handle = nullptr;
    bool     audio_inited = false;
    bool     audio_closing = false;
    bool     recording = false;
    uint16_t seq_num = 0;
    AdpcmState adpcm = {0, 0};
    TaskHandle_t audio_task = nullptr;
    TaskHandle_t teardown_task = nullptr;

    /* Response text buffer */
    std::string response_text;

    /* UI elements */
    lv_obj_t *screen = nullptr;
    lv_obj_t *main_cont = nullptr;
    lv_obj_t *mic_btn = nullptr;
    lv_obj_t *mic_label = nullptr;
    lv_obj_t *status_label = nullptr;
    lv_obj_t *ble_dot = nullptr;
    lv_obj_t *response_area = nullptr;
    lv_obj_t *response_label = nullptr;
    lv_obj_t *new_question_btn = nullptr;
    lv_obj_t *pulse_ring = nullptr;
};

static VaPrivate *s_priv = nullptr;

/* App instance is a long-lived singleton; LVGL deletes widgets on screen unload but s_priv kept stale
 * pointers. BLE/WiFi/async code must not touch freed objects — clear refs in close() and guard updates. */
static bool va_lv_attached(void)
{
    return s_priv && s_priv->screen && lv_obj_is_valid(s_priv->screen);
}

static void va_ui_detach(void)
{
    if (!s_priv) {
        return;
    }
    s_priv->screen = nullptr;
    s_priv->main_cont = nullptr;
    s_priv->mic_btn = nullptr;
    s_priv->mic_label = nullptr;
    s_priv->status_label = nullptr;
    s_priv->ble_dot = nullptr;
    s_priv->response_area = nullptr;
    s_priv->response_label = nullptr;
    s_priv->new_question_btn = nullptr;
    s_priv->pulse_ring = nullptr;
}

/* Audio teardown task — runs off the LVGL thread so close() doesn't freeze the display.
 * Waits for the audio streaming task to finish, then closes the codec. */
static void va_audio_teardown_task(void *arg)
{
    (void)arg;
    if (!s_priv) { vTaskDelete(nullptr); return; }

    /* Wait for audio task to self-delete (it checks s_priv->recording) */
    for (int i = 0; i < 150 && s_priv->audio_task != nullptr; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (s_priv->mic_handle) {
        esp_codec_dev_close(s_priv->mic_handle);
        esp_codec_dev_delete(s_priv->mic_handle);
        s_priv->mic_handle = nullptr;
    }
    s_priv->audio_inited = false;
    s_priv->audio_closing = false;
    s_priv->teardown_task = nullptr;
    vTaskDelete(nullptr);
}

static void va_audio_teardown(void)
{
    if (!s_priv) { return; }
    s_priv->recording = false;

    /* If no audio task is running and no mic handle, nothing to do */
    if (!s_priv->audio_task && !s_priv->mic_handle) {
        s_priv->audio_inited = false;
        s_priv->audio_closing = false;
        return;
    }

    if (s_priv->teardown_task) {
        return;
    }

    /* Offload the blocking wait + codec close to a background task
     * so close() returns immediately and the LVGL task keeps rendering */
    s_priv->audio_closing = true;
    if (xTaskCreatePinnedToCore(
            va_audio_teardown_task, "va_teardown", 4096, nullptr, 2, &s_priv->teardown_task, 0
        ) != pdPASS) {
        s_priv->audio_closing = false;
        s_priv->teardown_task = nullptr;
        ESP_UTILS_LOGE("Failed to start audio teardown task");
    }
}

/* ── IMA-ADPCM tables ── */
static const int16_t ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static uint8_t adpcm_encode_sample(int16_t sample, AdpcmState *state)
{
    int diff = sample - state->predicted;
    uint8_t nibble = 0;
    if (diff < 0) {
        nibble = 8;
        diff = -diff;
    }

    int16_t step = ima_step_table[state->index];
    int16_t diffq = step >> 3;

    if (diff >= step) { nibble |= 4; diff -= step; diffq += step; }
    step >>= 1;
    if (diff >= step) { nibble |= 2; diff -= step; diffq += step; }
    step >>= 1;
    if (diff >= step) { nibble |= 1; diffq += step; }

    if (nibble & 8) {
        state->predicted -= diffq;
    } else {
        state->predicted += diffq;
    }
    if (state->predicted > 32767) state->predicted = 32767;
    if (state->predicted < -32768) state->predicted = -32768;

    state->index += ima_index_table[nibble];
    if (state->index < 0) state->index = 0;
    if (state->index > 88) state->index = 88;

    return nibble & 0x0F;
}

static int adpcm_encode_block(const int16_t *pcm, int num_samples, uint8_t *out, AdpcmState *state)
{
    int out_len = 0;
    for (int i = 0; i < num_samples; i += 2) {
        uint8_t lo = adpcm_encode_sample(pcm[i], state);
        uint8_t hi = (i + 1 < num_samples) ? adpcm_encode_sample(pcm[i + 1], state) : 0;
        out[out_len++] = (hi << 4) | lo;
    }
    return out_len;
}

/* ── BLE GATTS ── */

static uint8_t va_service_uuid128[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x00, 0xAA, 0x00, 0x00
};

/* ANCS service UUID (7905F431-B5CE-4E99-A40F-4B1E122D00D0) little-endian
   Used as a solicited service in scan response so iOS exposes ANCS. */
static uint8_t ancs_solicitation_uuid128[16] = {
    0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4,
    0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79
};

static esp_ble_adv_params_t va_adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr         = {0},
    .peer_addr_type    = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_adv_data_t va_adv_data = {
    .set_scan_rsp        = false,
    .include_name        = false,
    .include_txpower     = false,
    .min_interval        = 0,
    .max_interval        = 0,
    .appearance          = 0,
    .manufacturer_len    = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len    = 0,
    .p_service_data      = nullptr,
    .service_uuid_len    = 16,
    .p_service_uuid      = va_service_uuid128,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t va_scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = false,
    .include_txpower     = false,
    .min_interval        = 0,
    .max_interval        = 0,
    .appearance          = 0,
    .manufacturer_len    = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len    = 0,
    .p_service_data      = nullptr,
    .service_uuid_len    = sizeof(ancs_solicitation_uuid128),
    .p_service_uuid      = ancs_solicitation_uuid128,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static uint8_t adv_config_done = 0;

static void update_ble_dot(bool connected)
{
    if (!s_priv || !s_priv->ble_dot || !lv_obj_is_valid(s_priv->ble_dot)) {
        return;
    }
    lv_color_t color = connected ? lv_color_hex(COLOR_GREEN) : lv_color_hex(COLOR_GRAY);
    lv_obj_set_style_bg_color(s_priv->ble_dot, color, 0);
}

static void set_state(VaState new_state);

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~ADV_CONFIG_FLAG);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&va_adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&va_adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_UTILS_LOGE("Advertising start failed");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        break;
    /* ── BLE security events (required for ANCS) ── */
    case ESP_GAP_BLE_NC_REQ_EVT:
        /* Numeric comparison — auto-accept (we have no display for 6-digit code) */
        ESP_UTILS_LOGI("BLE NC request, auto-accept");
        esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        /* Peer requests security — accept */
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ESP_UTILS_LOGI("BLE passkey notify: %06lu",
                       (unsigned long)param->ble_security.key_notif.passkey);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_UTILS_LOGI("BLE auth complete: addr=%02x:%02x:%02x:%02x:%02x:%02x success=%d",
                       bd_addr[0], bd_addr[1], bd_addr[2],
                       bd_addr[3], bd_addr[4], bd_addr[5],
                       param->ble_security.auth_cmpl.success);
        if (!param->ble_security.auth_cmpl.success) {
            ESP_UTILS_LOGE("BLE auth failed, reason=0x%x",
                           param->ble_security.auth_cmpl.fail_reason);
        }
        break;
    }
    default:
        break;
    }
}

/* Forward declaration */
static void stop_recording(void);

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        g_ble.gatts_if = gatts_if;
        esp_ble_gap_set_device_name(DEVICE_NAME);

        adv_config_done |= ADV_CONFIG_FLAG;
        esp_ble_gap_config_adv_data(&va_adv_data);
        adv_config_done |= SCAN_RSP_CONFIG_FLAG;
        esp_ble_gap_config_adv_data(&va_scan_rsp_data);

        /* Create service */
        esp_gatt_srvc_id_t service_id = {};
        service_id.is_primary = true;
        service_id.id.inst_id = 0;
        service_id.id.uuid.len = ESP_UUID_LEN_16;
        service_id.id.uuid.uuid.uuid16 = VA_SERVICE_UUID;
        /* 1 service + 6 chars * (char decl + char val) + 2 CCC = 15 handles */
        esp_ble_gatts_create_service(gatts_if, &service_id, 15);
        break;
    }
    case ESP_GATTS_CREATE_EVT: {
        g_ble.service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(g_ble.service_handle);

        /* Add Audio characteristic (NOTIFY) */
        esp_bt_uuid_t audio_uuid = {};
        audio_uuid.len = ESP_UUID_LEN_16;
        audio_uuid.uuid.uuid16 = VA_CHAR_AUDIO_UUID;
        esp_gatt_char_prop_t audio_prop = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_ble_gatts_add_char(g_ble.service_handle, &audio_uuid,
                               ESP_GATT_PERM_READ, audio_prop, nullptr, nullptr);
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
        if (param->add_char.char_uuid.uuid.uuid16 == VA_CHAR_AUDIO_UUID) {
            g_ble.audio_attr_handle = param->add_char.attr_handle;

            /* Add Text Response characteristic (WRITE) */
            esp_bt_uuid_t text_uuid = {};
            text_uuid.len = ESP_UUID_LEN_16;
            text_uuid.uuid.uuid16 = VA_CHAR_TEXT_UUID;
            esp_gatt_char_prop_t text_prop = ESP_GATT_CHAR_PROP_BIT_WRITE;
            esp_ble_gatts_add_char(g_ble.service_handle, &text_uuid,
                                   ESP_GATT_PERM_WRITE, text_prop, nullptr, nullptr);
        } else if (param->add_char.char_uuid.uuid.uuid16 == VA_CHAR_TEXT_UUID) {
            g_ble.text_attr_handle = param->add_char.attr_handle;

            /* Add Context sync characteristic (WRITE) */
            esp_bt_uuid_t ctx_uuid = {};
            ctx_uuid.len = ESP_UUID_LEN_16;
            ctx_uuid.uuid.uuid16 = VA_CHAR_CONTEXT_UUID;
            esp_gatt_char_prop_t ctx_prop = ESP_GATT_CHAR_PROP_BIT_WRITE;
            esp_ble_gatts_add_char(g_ble.service_handle, &ctx_uuid,
                                   ESP_GATT_PERM_WRITE, ctx_prop, nullptr, nullptr);
        } else if (param->add_char.char_uuid.uuid.uuid16 == VA_CHAR_CONTEXT_UUID) {
            g_ble.context_attr_handle = param->add_char.attr_handle;

            /* Add Notification characteristic (WRITE) */
            esp_bt_uuid_t notif_uuid = {};
            notif_uuid.len = ESP_UUID_LEN_16;
            notif_uuid.uuid.uuid16 = VA_CHAR_NOTIF_UUID;
            esp_gatt_char_prop_t notif_prop = ESP_GATT_CHAR_PROP_BIT_WRITE;
            esp_ble_gatts_add_char(g_ble.service_handle, &notif_uuid,
                                   ESP_GATT_PERM_WRITE, notif_prop, nullptr, nullptr);
        } else if (param->add_char.char_uuid.uuid.uuid16 == VA_CHAR_NOTIF_UUID) {
            g_ble.notif_attr_handle = param->add_char.attr_handle;
            esp_bt_uuid_t health_req_uuid = {};
            health_req_uuid.len = ESP_UUID_LEN_16;
            health_req_uuid.uuid.uuid16 = VA_CHAR_HEALTH_REQ_UUID;
            esp_gatt_char_prop_t health_req_prop = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
            esp_ble_gatts_add_char(g_ble.service_handle, &health_req_uuid,
                                   ESP_GATT_PERM_READ, health_req_prop, nullptr, nullptr);
        } else if (param->add_char.char_uuid.uuid.uuid16 == VA_CHAR_HEALTH_REQ_UUID) {
            g_ble.health_request_attr_handle = param->add_char.attr_handle;
            esp_bt_uuid_t health_data_uuid = {};
            health_data_uuid.len = ESP_UUID_LEN_16;
            health_data_uuid.uuid.uuid16 = VA_CHAR_HEALTH_DATA_UUID;
            esp_gatt_char_prop_t health_data_prop = ESP_GATT_CHAR_PROP_BIT_WRITE;
            esp_ble_gatts_add_char(g_ble.service_handle, &health_data_uuid,
                                   ESP_GATT_PERM_WRITE, health_data_prop, nullptr, nullptr);
        } else if (param->add_char.char_uuid.uuid.uuid16 == VA_CHAR_HEALTH_DATA_UUID) {
            g_ble.health_data_attr_handle = param->add_char.attr_handle;
        }
        break;
    }
    case ESP_GATTS_CONNECT_EVT: {
        g_ble.conn_id = param->connect.conn_id;
        g_ble.connected = true;
        /* Request MTU 512 */
        esp_ble_gatt_set_local_mtu(512);
        {
            LvLockGuard gui_guard;
            update_ble_dot(true);
        }
        /* Notify ANCS client so it can search for ANCS service on this connection */
        ancs_on_phone_connected(param->connect.conn_id,
                                param->connect.remote_bda,
                                param->connect.ble_addr_type);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT: {
        g_ble.connected = false;
        g_ble.audio_notify_enabled = false;
        g_ble.health_request_notify_enabled = false;
        keepalive_stop();
        ancs_on_phone_disconnected();
        if (s_priv && s_priv->recording) {
            stop_recording();
        }
        /* Restart advertising */
        esp_ble_gap_start_advertising(&va_adv_params);
        LvLockGuard gui_guard;
        update_ble_dot(false);
        if (s_priv && (s_priv->state == VA_STATE_LISTENING || s_priv->state == VA_STATE_PROCESSING)) {
            if (va_lv_attached()) {
                set_state(VA_STATE_IDLE);
            } else {
                s_priv->state = VA_STATE_IDLE;
            }
        }
        break;
    }
    case ESP_GATTS_MTU_EVT: {
        g_ble.mtu = param->mtu.mtu;
        ESP_UTILS_LOGI("MTU set to %d", g_ble.mtu);
        break;
    }
    case ESP_GATTS_WRITE_EVT: {
        if (param->write.handle == g_ble.text_attr_handle) {
            /* Receive text response from iPhone */
            uint8_t *data = param->write.value;
            uint16_t len = param->write.len;
            if (len >= 1 && s_priv) {
                uint8_t flags = data[0];
                if (len > 1) {
                    s_priv->response_text.append(reinterpret_cast<char *>(data + 1), len - 1);
                }
                if (flags == 0x01) {
                    if (s_priv->response_text.rfind("HEALTH|", 0) == 0) {
                        const std::string health_payload = s_priv->response_text.substr(7);
                        ESP_UTILS_LOGI("Health payload received via text channel: %s", health_payload.c_str());
                        health_app_on_ble_data_chunk(
                            reinterpret_cast<const uint8_t *>(health_payload.data()),
                            static_cast<uint16_t>(health_payload.size()), true
                        );
                        s_priv->response_text.clear();
                    } else {
                        /* Final chunk — display (only if VA UI is on-screen) */
                        LvLockGuard gui_guard;
                        if (va_lv_attached()) {
                            set_state(VA_STATE_RESPONSE);
                        }
                    }
                }
            }
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, nullptr);
            }
        } else if (param->write.handle == g_ble.context_attr_handle) {
            /* Context sync from iPhone: "unix_ts|temp_c|location" */
            std::string payload(reinterpret_cast<char *>(param->write.value), param->write.len);
            ESP_UTILS_LOGI("Context sync: %s", payload.c_str());

            /* Parse pipe-delimited fields */
            size_t p1 = payload.find('|');
            size_t p2 = (p1 != std::string::npos) ? payload.find('|', p1 + 1) : std::string::npos;

            if (p1 != std::string::npos && p2 != std::string::npos) {
                long ts = atol(payload.substr(0, p1).c_str());
                int temp = atoi(payload.substr(p1 + 1, p2 - p1 - 1).c_str());
                std::string location = payload.substr(p2 + 1);

                /* Update system time */
                struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
                settimeofday(&tv, nullptr);

                /* Update away screen weather */
                away_screen_set_weather(temp, location.c_str());
            }

            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, nullptr);
            }
        } else if (param->write.handle == g_ble.notif_attr_handle) {
            /* Notification from iPhone: CI=call incoming, CE=call ended, CM=call missed */
            std::string payload(reinterpret_cast<char *>(param->write.value), param->write.len);
            ESP_UTILS_LOGI("Notification: %s", payload.c_str());

            if (payload == "CI") {
                notifications_call_incoming();
            } else if (payload == "CE") {
                notifications_call_ended();
            } else if (payload == "CM") {
                notifications_call_missed();
            }

            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, nullptr);
            }
        } else if (param->write.handle == g_ble.health_data_attr_handle) {
            uint8_t *data = param->write.value;
            uint16_t len = param->write.len;
            if (len >= 1) {
                uint8_t flags = data[0];
                const uint8_t *payload = (len > 1) ? (data + 1) : nullptr;
                uint16_t payload_len = (len > 1) ? (len - 1) : 0;
                health_app_on_ble_data_chunk(payload, payload_len, flags == 0x01);
            }
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, nullptr);
            }
        } else {
            /* Check for CCC descriptor write (enable/disable notifications) */
            if (param->write.len == 2) {
                uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                uint16_t audio_ccc_handle = g_ble.audio_attr_handle ? (g_ble.audio_attr_handle + 1) : 0;
                uint16_t health_req_ccc_handle = g_ble.health_request_attr_handle ? (g_ble.health_request_attr_handle + 1) : 0;
                if (param->write.handle == audio_ccc_handle) {
                    g_ble.audio_notify_enabled = (descr_value == 0x0001);
                    if (g_ble.audio_notify_enabled) {
                        ESP_UTILS_LOGI("Audio notify enabled — starting keepalive");
                        keepalive_start();
                    } else {
                        keepalive_stop();
                    }
                } else if (param->write.handle == health_req_ccc_handle) {
                    g_ble.health_request_notify_enabled = (descr_value == 0x0001);
                    ESP_UTILS_LOGI(
                        "Health request notify %s",
                        g_ble.health_request_notify_enabled ? "enabled" : "disabled"
                    );
                }
            }
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, nullptr);
            }
        }
        break;
    }
    case ESP_GATTS_CONF_EVT:
        break;
    default:
        break;
    }
}

bool va_init_ble(void)
{
    if (g_ble.inited) return true;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret;

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_UTILS_LOGE("BT controller init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_UTILS_LOGE("BT controller enable failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_bluedroid_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_UTILS_LOGE("Bluedroid init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_UTILS_LOGE("Bluedroid enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(GATTS_APP_ID);
    esp_ble_gatt_set_local_mtu(512);

    /* ── BLE security (required for ANCS) ── */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t oob_support = ESP_BLE_OOB_DISABLE;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oob_support, sizeof(oob_support));

    /* ── ANCS GATT Client ── */
    if (!ancs_client_init()) {
        ESP_UTILS_LOGE("ANCS client init failed");
    }

    g_ble.inited = true;
    return true;
}

bool va_ble_is_connected(void)
{
    return g_ble.connected;
}

bool va_request_health_refresh(void)
{
    if (!g_ble.connected || !g_ble.health_request_notify_enabled || g_ble.gatts_if == ESP_GATT_IF_NONE ||
        g_ble.health_request_attr_handle == 0) {
        ESP_UTILS_LOGW(
            "Health refresh unavailable: connected=%d notify=%d handle=%u",
            g_ble.connected, g_ble.health_request_notify_enabled, g_ble.health_request_attr_handle
        );
        return false;
    }

    uint8_t request_byte = 'R';
    ESP_UTILS_LOGI("Sending health refresh request to iPhone");
    esp_err_t err = esp_ble_gatts_send_indicate(
        g_ble.gatts_if, g_ble.conn_id, g_ble.health_request_attr_handle, 1, &request_byte, false
    );
    if (err != ESP_OK) {
        ESP_UTILS_LOGW("Health refresh request failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

/* ── Audio capture ── */

static bool init_audio(void)
{
    if (!s_priv) {
        return false;
    }

    for (int i = 0; i < 100 && (s_priv->audio_closing || s_priv->teardown_task != nullptr); i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (s_priv->audio_closing || s_priv->teardown_task != nullptr) {
        ESP_UTILS_LOGW("Audio teardown still in progress");
        return false;
    }

    if (s_priv->audio_inited) return true;

    esp_err_t err = bsp_audio_init(NULL);
    if (err != ESP_OK) {
        ESP_UTILS_LOGE("BSP audio init failed: %s", esp_err_to_name(err));
        return false;
    }
    s_priv->mic_handle = bsp_audio_codec_microphone_init();
    if (!s_priv->mic_handle) {
        ESP_UTILS_LOGE("Microphone codec init failed");
        return false;
    }

    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate = AUDIO_SAMPLE_RATE;
    fs.bits_per_sample = AUDIO_BITS_PER_SAMPLE;
    fs.channel = AUDIO_CHANNELS;
    esp_codec_dev_open(s_priv->mic_handle, &fs);

    s_priv->audio_inited = true;
    return true;
}

static void audio_stream_task(void *param)
{
    (void)param;
    int16_t pcm_buf[PCM_CHUNK_SAMPLES];
    uint8_t adpcm_buf[ADPCM_CHUNK_BYTES];
    uint8_t ble_packet[BLE_PACKET_SIZE];

    while (s_priv && s_priv->recording) {
        esp_err_t err = esp_codec_dev_read(s_priv->mic_handle, pcm_buf, PCM_CHUNK_BYTES);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int adpcm_len = adpcm_encode_block(pcm_buf, PCM_CHUNK_SAMPLES, adpcm_buf, &s_priv->adpcm);

        /* Build BLE packet: [2-byte seq][ADPCM data] */
        int send_len = 2 + adpcm_len;
        if (send_len > BLE_PACKET_SIZE) send_len = BLE_PACKET_SIZE;

        ble_packet[0] = s_priv->seq_num & 0xFF;
        ble_packet[1] = (s_priv->seq_num >> 8) & 0xFF;
        memcpy(ble_packet + 2, adpcm_buf, send_len - 2);
        s_priv->seq_num++;

        if (g_ble.connected && g_ble.gatts_if != ESP_GATT_IF_NONE) {
            uint16_t len = send_len;
            esp_ble_gatts_send_indicate(g_ble.gatts_if, g_ble.conn_id,
                                        g_ble.audio_attr_handle, len, ble_packet, false);
        }
    }

    /* Send end-of-audio marker */
    if (s_priv && g_ble.connected) {
        uint8_t eoa[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        esp_ble_gatts_send_indicate(g_ble.gatts_if, g_ble.conn_id,
                                    g_ble.audio_attr_handle, 4, eoa, false);
    }

    if (s_priv) s_priv->audio_task = nullptr;
    vTaskDelete(NULL);
}

static bool start_recording(void)
{
    if (!s_priv || s_priv->recording) {
        return false;
    }
    if (!init_audio()) {
        return false;
    }

    s_priv->recording = true;
    s_priv->seq_num = 0;
    s_priv->adpcm = {0, 0};
    s_priv->response_text.clear();

    if (xTaskCreatePinnedToCore(audio_stream_task, "va_audio", 8192, nullptr, 3, &s_priv->audio_task, 1) != pdPASS) {
        s_priv->recording = false;
        ESP_UTILS_LOGE("Failed to start audio streaming task");
        return false;
    }

    return true;
}

static void stop_recording(void)
{
    if (!s_priv) return;
    s_priv->recording = false;
    /* Task will self-delete after sending end-of-audio */
}

/* ── UI ── */

static void start_pulse_anim(void)
{
    if (!s_priv || !s_priv->pulse_ring || !lv_obj_is_valid(s_priv->pulse_ring)) {
        return;
    }
    lv_obj_set_style_border_color(s_priv->pulse_ring, lv_color_hex(COLOR_RED), 0);
    lv_obj_set_style_border_width(s_priv->pulse_ring, 4, 0);
    lv_obj_set_style_border_opa(s_priv->pulse_ring, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_priv->pulse_ring, LV_OBJ_FLAG_HIDDEN);
}

static void stop_pulse_anim(void)
{
    if (!s_priv || !s_priv->pulse_ring || !lv_obj_is_valid(s_priv->pulse_ring)) {
        return;
    }
    lv_obj_add_flag(s_priv->pulse_ring, LV_OBJ_FLAG_HIDDEN);
}

static void set_state(VaState new_state)
{
    if (!s_priv || !va_lv_attached()) {
        return;
    }
    s_priv->state = new_state;

    /* Hide everything first */
    stop_pulse_anim();
    if (s_priv->response_area) lv_obj_add_flag(s_priv->response_area, LV_OBJ_FLAG_HIDDEN);
    if (s_priv->new_question_btn) lv_obj_add_flag(s_priv->new_question_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_priv->mic_btn) lv_obj_remove_flag(s_priv->mic_btn, LV_OBJ_FLAG_HIDDEN);

    switch (new_state) {
    case VA_STATE_IDLE:
        if (s_priv->status_label)
            lv_label_set_text(s_priv->status_label, "Tap to speak");
        if (s_priv->mic_label)
            lv_label_set_text(s_priv->mic_label, LV_SYMBOL_AUDIO);
        break;

    case VA_STATE_LISTENING:
        if (s_priv->status_label)
            lv_label_set_text(s_priv->status_label, "Listening...");
        if (s_priv->mic_label)
            lv_label_set_text(s_priv->mic_label, LV_SYMBOL_STOP);
        start_pulse_anim();
        break;

    case VA_STATE_PROCESSING:
        if (s_priv->status_label)
            lv_label_set_text(s_priv->status_label, "Processing...");
        if (s_priv->mic_btn) lv_obj_add_flag(s_priv->mic_btn, LV_OBJ_FLAG_HIDDEN);
        break;

    case VA_STATE_RESPONSE:
        if (s_priv->status_label)
            lv_label_set_text(s_priv->status_label, "Response");
        if (s_priv->mic_btn) lv_obj_add_flag(s_priv->mic_btn, LV_OBJ_FLAG_HIDDEN);
        if (s_priv->response_area && s_priv->response_label) {
            lv_label_set_text(s_priv->response_label, s_priv->response_text.c_str());
            lv_obj_remove_flag(s_priv->response_area, LV_OBJ_FLAG_HIDDEN);
            lv_obj_scroll_to_y(s_priv->response_area, 0, LV_ANIM_OFF);
        }
        if (s_priv->new_question_btn)
            lv_obj_remove_flag(s_priv->new_question_btn, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

static void mic_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!s_priv || !va_lv_attached()) {
        return;
    }

    if (!g_ble.connected) {
        if (s_priv->status_label)
            lv_label_set_text(s_priv->status_label, "Not connected");
        return;
    }

    switch (s_priv->state) {
    case VA_STATE_IDLE:
        if (start_recording()) {
            set_state(VA_STATE_LISTENING);
        } else if (s_priv->status_label) {
            lv_label_set_text(s_priv->status_label, "Mic busy, try again");
        }
        break;
    case VA_STATE_LISTENING:
        stop_recording();
        set_state(VA_STATE_PROCESSING);
        break;
    default:
        break;
    }
}

static void new_question_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!s_priv || !va_lv_attached()) {
        return;
    }
    s_priv->response_text.clear();
    set_state(VA_STATE_IDLE);
}

static void create_ui(lv_obj_t *screen)
{
    /* Full-screen container (same pattern as Settings app) */
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

    lv_obj_update_layout(screen);

    lv_coord_t scr_w = lv_obj_get_width(screen);
    lv_coord_t scr_h = lv_obj_get_height(screen);
    /* During enable_resize_visual_area, layout can be stale until updated; clamp to panel size */
    if (scr_w < 32) {
        scr_w = BSP_LCD_H_RES;
    }
    if (scr_h < 32) {
        scr_h = BSP_LCD_V_RES;
    }

    /* Header with title and BLE dot */
    lv_obj_t *header = lv_obj_create(cont);
    lv_obj_set_size(header, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_outline_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Voice Assistant");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* BLE status dot (in header, right side) */
    s_priv->ble_dot = lv_obj_create(header);
    lv_obj_set_size(s_priv->ble_dot, 12, 12);
    lv_obj_set_style_radius(s_priv->ble_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_priv->ble_dot, lv_color_hex(COLOR_GRAY), 0);
    lv_obj_set_style_bg_opa(s_priv->ble_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_priv->ble_dot, 0, 0);
    lv_obj_align(s_priv->ble_dot, LV_ALIGN_RIGHT_MID, -20, 0);

    /* Status label */
    s_priv->status_label = lv_label_create(cont);
    lv_obj_set_style_text_color(s_priv->status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(s_priv->status_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_priv->status_label, LV_ALIGN_TOP_MID, 0, 65);

    /* Pulse ring (around mic button area) */
    int btn_size = 120;
    int ring_size = btn_size + 20;
    s_priv->pulse_ring = lv_obj_create(cont);
    lv_obj_set_size(s_priv->pulse_ring, ring_size, ring_size);
    lv_obj_set_style_radius(s_priv->pulse_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_priv->pulse_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_priv->pulse_ring, lv_color_hex(COLOR_RED), 0);
    lv_obj_set_style_border_width(s_priv->pulse_ring, 4, 0);
    lv_obj_set_style_border_opa(s_priv->pulse_ring, LV_OPA_COVER, 0);
    lv_obj_align(s_priv->pulse_ring, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_flag(s_priv->pulse_ring, LV_OBJ_FLAG_HIDDEN);

    /* Mic button */
    s_priv->mic_btn = lv_obj_create(cont);
    lv_obj_set_size(s_priv->mic_btn, btn_size, btn_size);
    lv_obj_set_style_radius(s_priv->mic_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_priv->mic_btn, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_set_style_bg_opa(s_priv->mic_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_priv->mic_btn, 0, 0);
    /* No shadow: extra off-screen blend work causes many partial flushes (bad with QSPI + IO flush_done). */
    lv_obj_align(s_priv->mic_btn, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_flag(s_priv->mic_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_priv->mic_btn, mic_btn_cb, LV_EVENT_CLICKED, nullptr);

    /* Mic icon label */
    s_priv->mic_label = lv_label_create(s_priv->mic_btn);
    lv_obj_set_style_text_color(s_priv->mic_label, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_font(s_priv->mic_label, &lv_font_montserrat_40, 0);
    lv_label_set_text(s_priv->mic_label, LV_SYMBOL_AUDIO);
    lv_obj_center(s_priv->mic_label);

    /* No continuous animations here: with the 410x502 QSPI partial-buffer path they force constant
     * partial flushes and are a common source of visible tearing or stuck app transitions. */

    /* Response card */
    s_priv->response_area = lv_obj_create(cont);
    {
        lv_coord_t ta_w = scr_w - 40;
        lv_coord_t ta_h = scr_h - 160;
        if (ta_w < 80) {
            ta_w = 80;
        }
        if (ta_h < 80) {
            ta_h = 80;
        }
        lv_obj_set_size(s_priv->response_area, ta_w, ta_h);
    }
    lv_obj_align(s_priv->response_area, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(s_priv->response_area, lv_color_hex(COLOR_DARK_ROW), 0);
    lv_obj_set_style_bg_opa(s_priv->response_area, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_priv->response_area, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_priv->response_area, &lv_font_montserrat_18, 0);
    lv_obj_set_style_border_width(s_priv->response_area, 0, 0);
    lv_obj_set_style_radius(s_priv->response_area, 12, 0);
    lv_obj_set_style_pad_all(s_priv->response_area, 16, 0);
    lv_obj_set_scrollbar_mode(s_priv->response_area, LV_SCROLLBAR_MODE_AUTO);

    s_priv->response_label = lv_label_create(s_priv->response_area);
    lv_label_set_text(s_priv->response_label, "");
    lv_label_set_long_mode(s_priv->response_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_priv->response_label, lv_pct(100));
    lv_obj_set_style_text_color(s_priv->response_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_priv->response_label, &lv_font_montserrat_18, 0);
    lv_obj_add_flag(s_priv->response_area, LV_OBJ_FLAG_HIDDEN);

    /* New question button */
    s_priv->new_question_btn = lv_obj_create(cont);
    lv_obj_set_size(s_priv->new_question_btn, 200, 48);
    lv_obj_set_style_radius(s_priv->new_question_btn, 24, 0);
    lv_obj_set_style_bg_color(s_priv->new_question_btn, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_set_style_bg_opa(s_priv->new_question_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_priv->new_question_btn, 0, 0);
    lv_obj_align(s_priv->new_question_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(s_priv->new_question_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_priv->new_question_btn, new_question_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *nq_label = lv_label_create(s_priv->new_question_btn);
    lv_label_set_text(nq_label, "New question");
    lv_obj_set_style_text_color(nq_label, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_font(nq_label, &lv_font_montserrat_18, 0);
    lv_obj_center(nq_label);
    lv_obj_add_flag(s_priv->new_question_btn, LV_OBJ_FLAG_HIDDEN);

    /* Set initial state */
    set_state(VA_STATE_IDLE);
    update_ble_dot(g_ble.connected);
}

/* ── App lifecycle ── */

VoiceAssistantApp *VoiceAssistantApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new VoiceAssistantApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

VoiceAssistantApp::VoiceAssistantApp(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME, &quantum_watch_voice_assistant_icon_98_98, true, use_status_bar, use_navigation_bar)
{
    s_priv = new VaPrivate();
}

VoiceAssistantApp::~VoiceAssistantApp()
{
    delete s_priv;
    s_priv = nullptr;
    _instance = nullptr;
}

bool VoiceAssistantApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    lv_obj_t *screen = lv_scr_act();
    if (!screen || !s_priv) return false;

    /* Remove framework borders, set black background (same as Settings) */
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_outline_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    /* Match logical display size while core has resized hor/ver for the app visual area */
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

    /* Boot normally calls va_init_ble(); retry only if that path was skipped or failed */
    if (!g_ble.inited && !va_init_ble()) {
        ESP_UTILS_LOGE("BLE init failed");
    }

    return true;
}

bool VoiceAssistantApp::back(void)
{
    ESP_UTILS_LOGD("Back");
    if (s_priv && s_priv->state == VA_STATE_RESPONSE) {
        if (va_lv_attached()) {
            set_state(VA_STATE_IDLE);
        } else {
            s_priv->state = VA_STATE_IDLE;
        }
        return true;
    }
    if (s_priv && s_priv->recording) {
        stop_recording();
    }
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool VoiceAssistantApp::close(void)
{
    ESP_UTILS_LOGD("Close");
    if (s_priv && s_priv->recording) {
        stop_recording();
    }
    va_audio_teardown();
    stop_pulse_anim();
    va_ui_detach();
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, VoiceAssistantApp, APP_NAME, []() {
    return std::shared_ptr<VoiceAssistantApp>(VoiceAssistantApp::requestInstance(), [](VoiceAssistantApp *p) {});
})

} // namespace esp_brookesia::apps
