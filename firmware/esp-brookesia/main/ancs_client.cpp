/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Apple Notification Center Service (ANCS) GATT client for quantum-watch.
 *
 * The watch acts as a GATT client to iOS's ANCS service, receiving
 * notifications for all apps (WhatsApp, iMessage, Email, etc.).
 * Runs alongside the existing GATTS server on the same BLE connection.
 *
 * Protocol reference: Apple ANCS Specification (Bluetooth accessory guidelines)
 *
 * Flow:
 *  1. iPhone connects (via GATTS advertising)
 *  2. We open a GATTC virtual connection on the same link
 *  3. Search for ANCS service (UUID 7905F431-...)
 *  4. Subscribe to Notification Source + Data Source
 *  5. On new notification: request attributes via Control Point
 *  6. Parse response from Data Source, feed into notifications system
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "ancs_client.h"
#include "notifications.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatt_common_api.h"
#include "esp_gap_ble_api.h"
#include "esp_timer.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:ANCS"
#include "esp_lib_utils.h"

/* ── ANCS UUIDs (128-bit, little-endian) ── */

/* Service: 7905F431-B5CE-4E99-A40F-4B1E122D00D0 */
static const uint8_t ANCS_SERVICE_UUID[16] = {
    0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4,
    0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79
};

/* Notification Source: 9FBF120D-6301-42D9-8C58-25E699A21DBD */
static const uint8_t NOTIF_SOURCE_UUID[16] = {
    0xBD, 0x1D, 0xA2, 0x99, 0xE6, 0x25, 0x58, 0x8C,
    0xD9, 0x42, 0x01, 0x63, 0x0D, 0x12, 0xBF, 0x9F
};

/* Control Point: 69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9 */
static const uint8_t CONTROL_POINT_UUID[16] = {
    0xD9, 0xD9, 0xAA, 0xFD, 0xBD, 0x9B, 0x21, 0x98,
    0xA8, 0x49, 0xE1, 0x45, 0xF3, 0xD8, 0xD1, 0x69
};

/* Data Source: 22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB */
static const uint8_t DATA_SOURCE_UUID[16] = {
    0xFB, 0x7B, 0x7C, 0xCE, 0x6A, 0xB3, 0x44, 0xBE,
    0xB5, 0x4B, 0xD6, 0x24, 0xE9, 0xC6, 0xEA, 0x22
};

/* ── ANCS Protocol Constants ── */

/* EventID */
#define ANCS_EVT_ADDED     0
#define ANCS_EVT_MODIFIED  1
#define ANCS_EVT_REMOVED   2

/* EventFlags */
#define ANCS_FLAG_SILENT       (1 << 0)
#define ANCS_FLAG_IMPORTANT    (1 << 1)
#define ANCS_FLAG_PRE_EXISTING (1 << 2)

/* CategoryID */
#define ANCS_CAT_OTHER              0
#define ANCS_CAT_INCOMING_CALL      1
#define ANCS_CAT_MISSED_CALL        2
#define ANCS_CAT_VOICEMAIL          3
#define ANCS_CAT_SOCIAL             4
#define ANCS_CAT_SCHEDULE           5
#define ANCS_CAT_EMAIL              6
#define ANCS_CAT_NEWS               7
#define ANCS_CAT_HEALTH_FITNESS     8
#define ANCS_CAT_BUSINESS_FINANCE   9
#define ANCS_CAT_LOCATION           10
#define ANCS_CAT_ENTERTAINMENT      11

/* CommandID */
#define ANCS_CMD_GET_NOTIF_ATTR  0
#define ANCS_CMD_GET_APP_ATTR    1
#define ANCS_CMD_PERFORM_ACTION  2

/* NotificationAttributeID */
#define ANCS_ATTR_APP_ID    0
#define ANCS_ATTR_TITLE     1
#define ANCS_ATTR_SUBTITLE  2
#define ANCS_ATTR_MESSAGE   3
#define ANCS_ATTR_MSG_SIZE  4
#define ANCS_ATTR_DATE      5

/* ActionID */
#define ANCS_ACTION_POSITIVE 0
#define ANCS_ACTION_NEGATIVE 1

/* Max lengths for requested attributes */
#define ANCS_MAX_TITLE_LEN   64
#define ANCS_MAX_MSG_LEN     128

/* GATTC app ID (must differ from GATTS app ID 0) */
#define ANCS_GATTC_APP_ID  1

/* Data source reassembly buffer */
#define DATA_BUF_SIZE  1024
/* Timer timeout for fragmented data (500ms) */
#define DATA_TIMEOUT_US  500000

/* ── State ── */

static struct {
    esp_gatt_if_t gattc_if;
    uint16_t      conn_id;
    bool          connected;
    bool          service_found;
    uint16_t      service_start;
    uint16_t      service_end;
    uint16_t      notif_source_handle;
    uint16_t      data_source_handle;
    uint16_t      control_point_handle;
    uint16_t      mtu;
    uint8_t       remote_bda[6];
} s_ancs = {
    .gattc_if = ESP_GATT_IF_NONE,
    .conn_id = 0,
    .connected = false,
    .service_found = false,
    .service_start = 0,
    .service_end = 0,
    .notif_source_handle = 0,
    .data_source_handle = 0,
    .control_point_handle = 0,
    .mtu = 23,
    .remote_bda = {0},
};

/* Data source reassembly */
static uint8_t s_data_buf[DATA_BUF_SIZE];
static uint16_t s_data_len = 0;
static esp_timer_handle_t s_data_timer = nullptr;

/* ── Helpers ── */

static const char *category_name(uint8_t cat)
{
    switch (cat) {
    case ANCS_CAT_OTHER:            return "Other";
    case ANCS_CAT_INCOMING_CALL:    return "Incoming Call";
    case ANCS_CAT_MISSED_CALL:      return "Missed Call";
    case ANCS_CAT_VOICEMAIL:        return "Voicemail";
    case ANCS_CAT_SOCIAL:           return "Social";
    case ANCS_CAT_SCHEDULE:         return "Schedule";
    case ANCS_CAT_EMAIL:            return "Email";
    case ANCS_CAT_NEWS:             return "News";
    case ANCS_CAT_HEALTH_FITNESS:   return "Health";
    case ANCS_CAT_BUSINESS_FINANCE: return "Finance";
    case ANCS_CAT_LOCATION:         return "Location";
    case ANCS_CAT_ENTERTAINMENT:    return "Entertainment";
    default:                        return "Notification";
    }
}

/* ── Request notification attributes via Control Point ── */

static void request_notification_attributes(uint32_t uid)
{
    if (s_ancs.control_point_handle == 0) return;

    /*  Command format:
     *  [CommandID:1] [UID:4] [AttrID:1 [MaxLen:2]] ...
     */
    uint8_t cmd[32];
    int idx = 0;

    cmd[idx++] = ANCS_CMD_GET_NOTIF_ATTR;
    memcpy(&cmd[idx], &uid, 4);
    idx += 4;

    /* Request AppIdentifier (no max len needed) */
    cmd[idx++] = ANCS_ATTR_APP_ID;

    /* Request Title with max length */
    cmd[idx++] = ANCS_ATTR_TITLE;
    cmd[idx++] = ANCS_MAX_TITLE_LEN & 0xFF;
    cmd[idx++] = (ANCS_MAX_TITLE_LEN >> 8) & 0xFF;

    /* Request Message with max length */
    cmd[idx++] = ANCS_ATTR_MESSAGE;
    cmd[idx++] = ANCS_MAX_MSG_LEN & 0xFF;
    cmd[idx++] = (ANCS_MAX_MSG_LEN >> 8) & 0xFF;

    esp_ble_gattc_write_char(s_ancs.gattc_if, s_ancs.conn_id,
                              s_ancs.control_point_handle,
                              idx, cmd,
                              ESP_GATT_WRITE_TYPE_RSP,
                              ESP_GATT_AUTH_REQ_NONE);
}

/* ── Parse Data Source response ── */

static void parse_data_source(const uint8_t *data, uint16_t len)
{
    if (!data || len < 5) return;

    uint8_t cmd_id = data[0];
    if (cmd_id != ANCS_CMD_GET_NOTIF_ATTR) {
        ESP_UTILS_LOGI("Data source: non-notif-attr command %d", cmd_id);
        return;
    }

    uint32_t uid;
    memcpy(&uid, &data[1], 4);

    char app_id[128] = {0};
    char title[ANCS_MAX_TITLE_LEN + 1] = {0};
    char message[ANCS_MAX_MSG_LEN + 1] = {0};

    uint16_t pos = 5;
    while (pos < len) {
        if (pos + 3 > len) break;  /* Need at least AttrID + 2-byte length */

        uint8_t attr_id = data[pos++];
        uint16_t attr_len = data[pos] | (data[pos + 1] << 8);
        pos += 2;

        if (pos + attr_len > len) break;  /* Truncated */

        switch (attr_id) {
        case ANCS_ATTR_APP_ID: {
            uint16_t copy_len = (attr_len < sizeof(app_id) - 1) ? attr_len : sizeof(app_id) - 1;
            memcpy(app_id, &data[pos], copy_len);
            app_id[copy_len] = '\0';
            break;
        }
        case ANCS_ATTR_TITLE: {
            uint16_t copy_len = (attr_len < ANCS_MAX_TITLE_LEN) ? attr_len : ANCS_MAX_TITLE_LEN;
            memcpy(title, &data[pos], copy_len);
            title[copy_len] = '\0';
            break;
        }
        case ANCS_ATTR_MESSAGE: {
            uint16_t copy_len = (attr_len < ANCS_MAX_MSG_LEN) ? attr_len : ANCS_MAX_MSG_LEN;
            memcpy(message, &data[pos], copy_len);
            message[copy_len] = '\0';
            break;
        }
        default:
            break;
        }
        pos += attr_len;
    }

    ESP_UTILS_LOGI("ANCS notif uid=%" PRIu32 " app=%s title=%s msg=%.40s",
                   uid, app_id, title, message);

    /* Map app bundle ID to display name — only allow specific apps */
    const char *display_name = nullptr;
    bool is_whatsapp = false;

    if (strstr(app_id, "whatsapp") || strstr(app_id, "WhatsApp")) {
        display_name = "WhatsApp";
        is_whatsapp = true;
    }
    else if (strstr(app_id, "googlemail") || strstr(app_id, "Gmail") ||
             strstr(app_id, "google.Gmail")) {
        display_name = "Gmail";
    }
    else if (strstr(app_id, "Mail") || strstr(app_id, "apple.mobilemail")) {
        display_name = "Mail";
    }
    else if (strstr(app_id, "mobilecal") || strstr(app_id, "Calendar") ||
             strstr(app_id, "googlecalendar") || strstr(app_id, "calendar")) {
        display_name = "Calendar";
    }
    else if (strstr(app_id, "mobilephone") || strstr(app_id, "InCallService")) {
        display_name = "Phone";
    }

    /* Drop notifications from apps we don't care about */
    if (!display_name) {
        ESP_UTILS_LOGI("ANCS: ignoring notification from %s", app_id);
        return;
    }

    /* WhatsApp audio/video calls show as notification with call-related title */
    if (is_whatsapp && title[0] &&
        (strstr(message, "call") || strstr(message, "Call") ||
         strstr(title, "call") || strstr(title, "Call") ||
         strstr(message, "ring") || strstr(message, "Ring"))) {
        ESP_UTILS_LOGI("WhatsApp call detected — showing call overlay");
        notifications_call_incoming();
        return;
    }

    notifications_add(display_name, title, message, 0, uid);
}

/* ── Data source timer callback (for fragmented responses) ── */

static void data_timer_cb(void *arg)
{
    (void)arg;
    esp_timer_stop(s_data_timer);
    if (s_data_len > 0) {
        parse_data_source(s_data_buf, s_data_len);
        s_data_len = 0;
    }
}

/* ── Handle Notification Source (8-byte event from iOS) ── */

static void handle_notification_source(const uint8_t *data, uint16_t len)
{
    if (len < 8) return;

    uint8_t event_id    = data[0];
    uint8_t event_flags = data[1];
    uint8_t category_id = data[2];
    uint8_t cat_count   = data[3];
    uint32_t uid;
    memcpy(&uid, &data[4], 4);

    ESP_UTILS_LOGI("ANCS event=%d flags=0x%02x cat=%s(%d) count=%d uid=%" PRIu32,
                   event_id, event_flags, category_name(category_id),
                   category_id, cat_count, uid);

    switch (event_id) {
    case ANCS_EVT_ADDED:
        /* Skip silent/pre-existing notifications */
        if (event_flags & ANCS_FLAG_SILENT) break;
        if (event_flags & ANCS_FLAG_PRE_EXISTING) break;

        if (category_id == ANCS_CAT_INCOMING_CALL) {
            notifications_call_incoming();
        } else if (category_id == ANCS_CAT_MISSED_CALL) {
            notifications_call_missed();
        } else {
            /* Request full details for all other notifications */
            request_notification_attributes(uid);
        }
        break;

    case ANCS_EVT_REMOVED:
        if (category_id == ANCS_CAT_INCOMING_CALL) {
            notifications_call_ended();
        } else {
            notifications_remove(uid);
        }
        break;

    case ANCS_EVT_MODIFIED:
        /* Could re-request attributes, but for now just ignore */
        break;
    }
}

/* ── GATTC Service/Characteristic Discovery ── */

static void discover_characteristics(esp_gatt_if_t gattc_if)
{
    uint16_t count = 0;
    esp_gatt_status_t ret;

    ret = esp_ble_gattc_get_attr_count(gattc_if, s_ancs.conn_id,
                                        ESP_GATT_DB_CHARACTERISTIC,
                                        s_ancs.service_start,
                                        s_ancs.service_end,
                                        0, &count);
    if (ret != ESP_GATT_OK || count == 0) {
        ESP_UTILS_LOGE("No ANCS characteristics found (ret=%d, count=%d)", ret, count);
        return;
    }

    esp_gattc_char_elem_t *chars = (esp_gattc_char_elem_t *)malloc(
        sizeof(esp_gattc_char_elem_t) * count);
    if (!chars) {
        ESP_UTILS_LOGE("Failed to allocate char array");
        return;
    }

    ret = esp_ble_gattc_get_all_char(gattc_if, s_ancs.conn_id,
                                      s_ancs.service_start,
                                      s_ancs.service_end,
                                      chars, &count, 0);
    if (ret != ESP_GATT_OK) {
        ESP_UTILS_LOGE("get_all_char failed: %d", ret);
        free(chars);
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        if (chars[i].uuid.len != ESP_UUID_LEN_128) continue;

        if (memcmp(chars[i].uuid.uuid.uuid128, NOTIF_SOURCE_UUID, 16) == 0) {
            s_ancs.notif_source_handle = chars[i].char_handle;
            ESP_UTILS_LOGI("Found Notification Source handle=%d", chars[i].char_handle);
            esp_ble_gattc_register_for_notify(gattc_if, s_ancs.remote_bda,
                                               chars[i].char_handle);
        } else if (memcmp(chars[i].uuid.uuid.uuid128, DATA_SOURCE_UUID, 16) == 0) {
            s_ancs.data_source_handle = chars[i].char_handle;
            ESP_UTILS_LOGI("Found Data Source handle=%d", chars[i].char_handle);
            esp_ble_gattc_register_for_notify(gattc_if, s_ancs.remote_bda,
                                               chars[i].char_handle);
        } else if (memcmp(chars[i].uuid.uuid.uuid128, CONTROL_POINT_UUID, 16) == 0) {
            s_ancs.control_point_handle = chars[i].char_handle;
            ESP_UTILS_LOGI("Found Control Point handle=%d", chars[i].char_handle);
        }
    }

    free(chars);
}

/* ── Enable CCCD for a characteristic (required for NOTIFY) ── */

static void enable_cccd(esp_gatt_if_t gattc_if, uint16_t char_handle)
{
    uint16_t count = 0;
    esp_gatt_status_t ret;

    ret = esp_ble_gattc_get_attr_count(gattc_if, s_ancs.conn_id,
                                        ESP_GATT_DB_DESCRIPTOR,
                                        s_ancs.service_start,
                                        s_ancs.service_end,
                                        char_handle, &count);
    if (ret != ESP_GATT_OK || count == 0) return;

    esp_gattc_descr_elem_t *descrs = (esp_gattc_descr_elem_t *)malloc(
        sizeof(esp_gattc_descr_elem_t) * count);
    if (!descrs) return;

    ret = esp_ble_gattc_get_all_descr(gattc_if, s_ancs.conn_id,
                                       char_handle, descrs, &count, 0);
    if (ret == ESP_GATT_OK) {
        uint8_t notify_en[2] = {0x01, 0x00};
        for (uint16_t i = 0; i < count; i++) {
            if (descrs[i].uuid.len == ESP_UUID_LEN_16 &&
                descrs[i].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
                esp_ble_gattc_write_char_descr(gattc_if, s_ancs.conn_id,
                                                descrs[i].handle,
                                                sizeof(notify_en), notify_en,
                                                ESP_GATT_WRITE_TYPE_RSP,
                                                ESP_GATT_AUTH_REQ_NONE);
                break;
            }
        }
    }
    free(descrs);
}

/* ── GATTC Event Handler ── */

void ancs_gattc_event_handler(esp_gattc_cb_event_t event,
                               esp_gatt_if_t gattc_if,
                               esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            s_ancs.gattc_if = gattc_if;
            ESP_UTILS_LOGI("ANCS GATTC registered (if=%d)", gattc_if);
        }
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK) {
            ESP_UTILS_LOGE("ANCS GATTC open failed: 0x%x", param->open.status);
            break;
        }
        s_ancs.conn_id = param->open.conn_id;
        s_ancs.connected = true;
        ESP_UTILS_LOGI("ANCS GATTC open, conn_id=%d", param->open.conn_id);

        /* Request encryption (required for ANCS) */
        esp_ble_set_encryption(param->open.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);

        /* Search for ANCS service */
        esp_bt_uuid_t uuid;
        uuid.len = ESP_UUID_LEN_128;
        memcpy(uuid.uuid.uuid128, ANCS_SERVICE_UUID, 16);
        esp_ble_gattc_search_service(gattc_if, s_ancs.conn_id, &uuid);
        break;

    case ESP_GATTC_SEARCH_RES_EVT:
        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128) {
            s_ancs.service_start = param->search_res.start_handle;
            s_ancs.service_end = param->search_res.end_handle;
            s_ancs.service_found = true;
            ESP_UTILS_LOGI("ANCS service found: handles %d–%d",
                          s_ancs.service_start, s_ancs.service_end);
        }
        break;

    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (s_ancs.service_found) {
            ESP_UTILS_LOGI("ANCS service discovery complete, finding characteristics");
            discover_characteristics(gattc_if);
        } else {
            ESP_UTILS_LOGW("ANCS service not found on this device");
        }
        break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        if (param->reg_for_notify.status == ESP_GATT_OK) {
            enable_cccd(gattc_if, param->reg_for_notify.handle);
        }
        break;

    case ESP_GATTC_CFG_MTU_EVT:
        s_ancs.mtu = param->cfg_mtu.mtu;
        break;

    case ESP_GATTC_NOTIFY_EVT:
        if (param->notify.handle == s_ancs.notif_source_handle) {
            handle_notification_source(param->notify.value, param->notify.value_len);
        } else if (param->notify.handle == s_ancs.data_source_handle) {
            /* Buffer fragmented Data Source responses */
            if (s_data_len + param->notify.value_len > DATA_BUF_SIZE) {
                ESP_UTILS_LOGE("Data source buffer overflow, discarding");
                s_data_len = 0;
                break;
            }
            memcpy(&s_data_buf[s_data_len], param->notify.value, param->notify.value_len);
            s_data_len += param->notify.value_len;

            /* If packet fills MTU, expect more fragments */
            if (param->notify.value_len == (s_ancs.mtu - 3)) {
                esp_timer_stop(s_data_timer);
                esp_timer_start_once(s_data_timer, DATA_TIMEOUT_US);
            } else {
                esp_timer_stop(s_data_timer);
                parse_data_source(s_data_buf, s_data_len);
                s_data_len = 0;
            }
        }
        break;

    case ESP_GATTC_WRITE_CHAR_EVT:
        if (param->write.status != ESP_GATT_OK) {
            ESP_UTILS_LOGE("ANCS write failed: 0x%x", param->write.status);
        }
        break;

    case ESP_GATTC_WRITE_DESCR_EVT:
        if (param->write.status != ESP_GATT_OK) {
            ESP_UTILS_LOGE("ANCS write descr failed: 0x%x", param->write.status);
        }
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        ESP_UTILS_LOGI("ANCS GATTC disconnected");
        s_ancs.connected = false;
        s_ancs.service_found = false;
        s_ancs.notif_source_handle = 0;
        s_ancs.data_source_handle = 0;
        s_ancs.control_point_handle = 0;
        s_data_len = 0;
        break;

    case ESP_GATTC_CONNECT_EVT:
        memcpy(s_ancs.remote_bda, param->connect.remote_bda, 6);
        break;

    default:
        break;
    }
}

/* ── Public API ── */

bool ancs_client_init(void)
{
    /* Create data source reassembly timer */
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = data_timer_cb;
    timer_args.name = "ancs_data";
    esp_err_t ret = esp_timer_create(&timer_args, &s_data_timer);
    if (ret != ESP_OK) {
        ESP_UTILS_LOGE("Failed to create ANCS data timer: %s", esp_err_to_name(ret));
        return false;
    }

    /* Register GATTC callback and app */
    ret = esp_ble_gattc_register_callback(
        [](esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
           esp_ble_gattc_cb_param_t *param) {
            /* Dispatch to our handler if it matches our interface */
            if (event == ESP_GATTC_REG_EVT ||
                gattc_if == ESP_GATT_IF_NONE ||
                gattc_if == s_ancs.gattc_if) {
                ancs_gattc_event_handler(event, gattc_if, param);
            }
        });
    if (ret != ESP_OK) {
        ESP_UTILS_LOGE("GATTC register callback failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_ble_gattc_app_register(ANCS_GATTC_APP_ID);
    if (ret != ESP_OK) {
        ESP_UTILS_LOGE("GATTC app register failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_UTILS_LOGI("ANCS client initialized");
    return true;
}

void ancs_on_phone_connected(uint16_t conn_id, const uint8_t *bda,
                              uint8_t addr_type)
{
    if (s_ancs.gattc_if == ESP_GATT_IF_NONE) return;

    memcpy(s_ancs.remote_bda, bda, 6);

    /* Open a GATTC virtual connection on the same physical link */
    esp_ble_gattc_open(s_ancs.gattc_if, (uint8_t *)bda,
                       (esp_ble_addr_type_t)addr_type, true);

    ESP_UTILS_LOGI("ANCS: opening GATTC connection to phone (addr_type=%d)", addr_type);
}

void ancs_on_phone_disconnected(void)
{
    s_ancs.connected = false;
    s_ancs.service_found = false;
    s_ancs.notif_source_handle = 0;
    s_ancs.data_source_handle = 0;
    s_ancs.control_point_handle = 0;
    s_data_len = 0;
}
