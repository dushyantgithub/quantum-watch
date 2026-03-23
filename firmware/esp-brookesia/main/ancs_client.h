/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Apple Notification Center Service (ANCS) GATT client.
 * Subscribes to iOS notifications over BLE and feeds them
 * into the watch notification system.
 */
#pragma once

#include "esp_gattc_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ANCS GATT client.
 *
 * Registers the GATTC callback and app. Must be called after
 * Bluedroid is enabled (inside va_init_ble, after GATTS setup).
 *
 * @return true on success
 */
bool ancs_client_init(void);

/**
 * @brief GATTC event handler — called from the main GATTC dispatcher.
 */
void ancs_gattc_event_handler(esp_gattc_cb_event_t event,
                               esp_gatt_if_t gattc_if,
                               esp_ble_gattc_cb_param_t *param);

/**
 * @brief Notify ANCS client that the phone connected.
 *        Triggers ANCS service search on the existing connection.
 *
 * @param conn_id   GATT connection ID (from GATTS CONNECT event)
 * @param bda       Remote device Bluetooth address
 * @param addr_type Remote device address type
 */
void ancs_on_phone_connected(uint16_t conn_id, const uint8_t *bda,
                              uint8_t addr_type);

/**
 * @brief Notify ANCS client that the phone disconnected.
 */
void ancs_on_phone_disconnected(void);

#ifdef __cplusplus
}
#endif
