/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Button driver for Waveshare ESP32-S3-Touch-AMOLED-2.06
 * PWR and BOOT physical buttons.
 */
#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO pins for ESP32-S3-Touch-AMOLED-2.06
 * BOOT=GPIO0 (active-low, pull-up)
 * PWR=GPIO10 (active-high, pull-down) per Waveshare docs */
#define BUTTON_BOOT_GPIO   (GPIO_NUM_0)
#define BUTTON_PWR_GPIO    (GPIO_NUM_10)

/* Long press threshold in ms (for BOOT: shutdown) */
#define BUTTON_LONG_PRESS_MS   (2000)

typedef void (*button_pwr_cb_t)(void *user_data);
typedef void (*button_pwr_long_cb_t)(void *user_data);
typedef void (*button_boot_short_cb_t)(void *user_data);
typedef void (*button_boot_long_cb_t)(void *user_data);

/**
 * @brief Initialize button GPIOs and start polling task.
 *
 * @return ESP_OK on success
 */
esp_err_t buttons_init(void);

/**
 * @brief Register PWR button callback (invoked on any press).
 *
 * @param cb Callback to invoke
 * @param user_data User data passed to callback
 */
void buttons_register_pwr_cb(button_pwr_cb_t cb, void *user_data);

/**
 * @brief Register PWR long-press callback (held >= 2s).
 */
void buttons_register_pwr_long_cb(button_pwr_long_cb_t cb, void *user_data);

/**
 * @brief Register BOOT short-press callback.
 *
 * @param cb Callback to invoke
 * @param user_data User data passed to callback
 */
void buttons_register_boot_short_cb(button_boot_short_cb_t cb, void *user_data);

/**
 * @brief Register BOOT long-press callback.
 *
 * @param cb Callback to invoke
 * @param user_data User data passed to callback
 */
void buttons_register_boot_long_cb(button_boot_long_cb_t cb, void *user_data);

/**
 * @brief Set away mode. When active, button presses call the wake callback
 *        instead of their normal callbacks.
 */
void buttons_set_away_mode(bool active);
bool buttons_get_away_mode(void);

typedef void (*button_wake_cb_t)(void *user_data);
void buttons_register_wake_cb(button_wake_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
