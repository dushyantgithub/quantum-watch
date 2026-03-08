/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Button driver for Waveshare ESP32-S3-Touch-AMOLED-2.06.
 * Polling-based detection for short/long press.
 */
#include "buttons.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define POLL_INTERVAL_MS  50
#define DEBOUNCE_MS       30

static button_pwr_cb_t s_pwr_cb = NULL;
static void *s_pwr_user = NULL;
static button_boot_short_cb_t s_boot_short_cb = NULL;
static void *s_boot_short_user = NULL;
static button_boot_long_cb_t s_boot_long_cb = NULL;
static void *s_boot_long_user = NULL;
static button_wake_cb_t s_wake_cb = NULL;
static void *s_wake_user = NULL;
static volatile bool s_away_mode = false;

static void button_task(void *arg)
{
    bool boot_pressed = false;
    bool pwr_pressed = false;
    uint32_t boot_press_start = 0;
    bool boot_long_fired = false;

    while (1) {
        int boot_level = gpio_get_level(BUTTON_BOOT_GPIO);
        int pwr_level = gpio_get_level(BUTTON_PWR_GPIO);

        /* BOOT is active-low (pressed = 0), PWR is active-high (pressed = 1) */
        bool boot_now = (boot_level == 0);
        bool pwr_now = (pwr_level == 1);

        /* BOOT button handling */
        if (boot_now) {
            if (!boot_pressed) {
                boot_pressed = true;
                boot_press_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
                boot_long_fired = false;
            } else if (!s_away_mode) {
                uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - boot_press_start;
                if (!boot_long_fired && elapsed >= BUTTON_LONG_PRESS_MS) {
                    boot_long_fired = true;
                    if (s_boot_long_cb) {
                        s_boot_long_cb(s_boot_long_user);
                    }
                }
            }
        } else {
            if (boot_pressed) {
                if (s_away_mode) {
                    if (s_wake_cb) { s_wake_cb(s_wake_user); }
                } else if (!boot_long_fired) {
                    uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - boot_press_start;
                    if (elapsed >= DEBOUNCE_MS) {
                        if (s_boot_short_cb) {
                            s_boot_short_cb(s_boot_short_user);
                        }
                    }
                }
            }
            boot_pressed = false;
        }

        /* PWR button handling (simple press) */
        if (pwr_now) {
            if (!pwr_pressed) {
                pwr_pressed = true;
                if (s_away_mode) {
                    if (s_wake_cb) { s_wake_cb(s_wake_user); }
                } else if (s_pwr_cb) {
                    s_pwr_cb(s_pwr_user);
                }
            }
        } else {
            pwr_pressed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t buttons_init(void)
{
    /* BOOT: active-low, needs pull-up */
    gpio_config_t boot_io = {
        .pin_bit_mask = (1ULL << BUTTON_BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&boot_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BOOT GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* PWR: active-high, needs pull-down */
    gpio_config_t pwr_io = {
        .pin_bit_mask = (1ULL << BUTTON_PWR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&pwr_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWR GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t t = xTaskCreate(button_task, "buttons", 2048, NULL, 5, NULL);
    if (t != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Buttons initialized (BOOT=GPIO%d, PWR=GPIO%d)", BUTTON_BOOT_GPIO, BUTTON_PWR_GPIO);
    return ESP_OK;
}

void buttons_register_pwr_cb(button_pwr_cb_t cb, void *user_data)
{
    s_pwr_cb = cb;
    s_pwr_user = user_data;
}

void buttons_register_boot_short_cb(button_boot_short_cb_t cb, void *user_data)
{
    s_boot_short_cb = cb;
    s_boot_short_user = user_data;
}

void buttons_register_boot_long_cb(button_boot_long_cb_t cb, void *user_data)
{
    s_boot_long_cb = cb;
    s_boot_long_user = user_data;
}

void buttons_set_away_mode(bool active)
{
    s_away_mode = active;
}

bool buttons_get_away_mode(void)
{
    return s_away_mode;
}

void buttons_register_wake_cb(button_wake_cb_t cb, void *user_data)
{
    s_wake_cb = cb;
    s_wake_user = user_data;
}
