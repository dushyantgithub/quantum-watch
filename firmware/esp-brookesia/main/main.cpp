/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"
#include "buttons.h"
#include "esp_system.h"
#include "voice_assistant.h"
#include "notifications.h"
#include "startup_logo.h"
#include "vokrr_network.h"
#include "vokrr_os.h"
#include "vokrr_state.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "display/lv_display_private.h"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;

#define LVGL_PORT_INIT_CONFIG() \
    {                               \
        .task_priority = 5,       \
        .task_stack = 16 * 1024,       \
        .task_affinity = 1,       \
        .task_max_sleep_ms = 10,  \
        .timer_period_ms = 5,     \
    }

/* AXP2101 PMIC on the Waveshare AMOLED board */
#define AXP2101_I2C_ADDR         0x34U
#define AXP2101_REG_STATUS       0x00U
#define AXP2101_REG_VBAT_H       0x34U
#define AXP2101_CHG_STAT_MASK    0xC0U
#define AXP2101_VBAT_MV_PER_LSB  1U

struct StatusBarBatteryInfo {
    int percent = -1;
    bool charging = false;
    bool available = false;
};

static volatile int s_status_bar_battery_percent = -1;
static volatile bool s_status_bar_battery_charging = false;
static volatile bool s_status_bar_battery_available = false;
static volatile bool s_status_bar_battery_ready = false;
static TaskHandle_t s_status_bar_battery_task = nullptr;

static bool status_bar_battery_read_axp2101(StatusBarBatteryInfo *out)
{
    if (!out) {
        return false;
    }

    *out = {};

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        return false;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    i2c_master_dev_handle_t dev = nullptr;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK || !dev) {
        return false;
    }

    uint8_t reg_addr = AXP2101_REG_STATUS;
    uint8_t status = 0;
    err = i2c_master_transmit_receive(dev, &reg_addr, 1, &status, 1, 100);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(dev);
        return false;
    }

    /* Treat charging and charge-complete as externally powered so the charging icon is shown whenever USB-C is plugged in. */
    out->charging = (status & AXP2101_CHG_STAT_MASK) != 0;

    reg_addr = AXP2101_REG_VBAT_H;
    uint8_t vbat_buf[2] = {0};
    err = i2c_master_transmit_receive(dev, &reg_addr, 1, vbat_buf, 2, 100);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        return false;
    }

    int raw = ((vbat_buf[0] & 0x0F) << 8) | vbat_buf[1];
    int voltage_mv = raw * AXP2101_VBAT_MV_PER_LSB;
    if (voltage_mv <= 3000) {
        out->percent = 0;
    } else if (voltage_mv >= 4200) {
        out->percent = 100;
    } else {
        out->percent = ((voltage_mv - 3000) * 100) / 1200;
    }

    out->available = true;
    return true;
}

static void status_bar_battery_read_task(void *arg)
{
    (void)arg;

    StatusBarBatteryInfo info = {};
    status_bar_battery_read_axp2101(&info);

    s_status_bar_battery_percent = info.percent;
    s_status_bar_battery_charging = info.charging;
    s_status_bar_battery_available = info.available;
    s_status_bar_battery_ready = true;
    s_status_bar_battery_task = nullptr;

    vTaskDelete(nullptr);
}

static void status_bar_battery_request_refresh()
{
    if (!s_status_bar_battery_task) {
        xTaskCreatePinnedToCore(
            status_bar_battery_read_task, "status_bat", 3072, nullptr, 2, &s_status_bar_battery_task, 0
        );
    }
}

/* ── Flush-wait callback ──
 * LVGL 9's default wait_for_flushing() does `while(disp->flushing);` — a bare
 * spin with no timeout.  If the SPI DMA completion ISR is ever delayed or lost
 * (e.g. interrupt storm on core 0), the LVGL task hangs permanently and triggers
 * the task watchdog.
 *
 * Registering a flush_wait_cb replaces the bare spin.  We spin-poll just like
 * the original (no latency penalty) but with a 50 ms timeout.  After the
 * callback returns, LVGL unconditionally clears `flushing = 0`, so recovery is
 * guaranteed even if the ISR was truly lost. */
static void display_flush_wait_cb(lv_display_t *disp)
{
    /* Spin-poll (same speed as the default bare spin) but with a hard timeout */
    int64_t deadline = esp_timer_get_time() + 50000; /* 50 ms */
    while (disp->flushing) {
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGW("LVGL", "Flush wait timeout — forcing recovery");
            break;
        }
    }
}

static void startup_splash_set_opa(void *var, int32_t value)
{
    auto *obj = static_cast<lv_obj_t *>(var);
    if (obj && lv_obj_is_valid(obj)) {
        lv_obj_set_style_opa(obj, static_cast<lv_opa_t>(value), 0);
    }
}

static void startup_splash_delete(lv_anim_t *anim)
{
    auto *overlay = static_cast<lv_obj_t *>(anim->user_data);
    if (overlay && lv_obj_is_valid(overlay)) {
        lv_obj_delete(overlay);
    }
}

static void startup_splash_show()
{
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(overlay);

    lv_obj_t *logo = lv_image_create(overlay);
    lv_image_set_src(logo, &quantum_watch_startup_logo_220_220);
    lv_obj_set_style_opa(logo, LV_OPA_TRANSP, 0);
    lv_obj_center(logo);

    lv_anim_t fade_in;
    lv_anim_init(&fade_in);
    lv_anim_set_var(&fade_in, logo);
    lv_anim_set_exec_cb(&fade_in, startup_splash_set_opa);
    lv_anim_set_values(&fade_in, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&fade_in, 320);
    lv_anim_set_delay(&fade_in, 80);
    lv_anim_set_path_cb(&fade_in, lv_anim_path_ease_out);
    lv_anim_start(&fade_in);

    lv_anim_t fade_out;
    lv_anim_init(&fade_out);
    lv_anim_set_var(&fade_out, logo);
    lv_anim_set_user_data(&fade_out, overlay);
    lv_anim_set_exec_cb(&fade_out, startup_splash_set_opa);
    lv_anim_set_values(&fade_out, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&fade_out, 280);
    lv_anim_set_delay(&fade_out, 900);
    lv_anim_set_path_cb(&fade_out, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&fade_out, startup_splash_delete);
    lv_anim_start(&fade_out);
}

extern "C" void app_main(void)
{
    ESP_UTILS_LOGI("Vokrr Watch OS starting");

    /* BLE bonding/config persistence depends on NVS. Settings used to initialize this as a side effect,
     * so keep it explicit here now that Settings is removed from the build. */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    vokrr::state_init();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = LVGL_PORT_INIT_CONFIG(),
    };
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    ESP_UTILS_CHECK_NULL_EXIT(disp, "Start display failed");
    ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Turn on display backlight failed");

    /* Replace LVGL's bare-spin flush wait with a safe timeout-based callback */
    lv_display_set_flush_wait_cb(disp, display_flush_wait_cb);

    /* Configure GUI lock */
    LvLock::registerCallbacks([](int timeout_ms) {
        if (timeout_ms < 0) {
            timeout_ms = 0;
        } else if (timeout_ms == 0) {
            timeout_ms = 1;
        }
        ESP_UTILS_CHECK_FALSE_RETURN(bsp_display_lock(timeout_ms), false, "Lock failed");

        return true;
    }, []() {
        bsp_display_unlock();

        return true;
    });

    {
        // When operating on non-GUI tasks, acquire the LVGL lock first.
        LvLockGuard gui_guard;

        /* Keep boot visuals lightweight: one centered image over black with opacity-only animation. */
        vokrr::os_init();
        startup_splash_show();

        /* Publish asynchronous PMIC results into the Vokrr state model. */
        lv_timer_create([](lv_timer_t *) {
            if (s_status_bar_battery_ready) {
                s_status_bar_battery_ready = false;
                if (s_status_bar_battery_available) {
                    vokrr::state_set_battery(
                        s_status_bar_battery_percent, s_status_bar_battery_charging
                    );
                }
            }
        }, 1000, nullptr);

        /* Keep PMIC reads off the LVGL task to avoid UI stalls. */
        lv_timer_create([](lv_timer_t *t) {
            (void)t;
            status_bar_battery_request_refresh();
        }, 15000, nullptr);
        status_bar_battery_request_refresh();

        /* Initialize notification system (call overlay + drawer) */
        notifications_init();
    }

    ESP_LOGI("Vokrr", "UI ready (internal heap %u bytes, largest %u; PSRAM %u bytes)",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    /* Initialize the larger WiFi pools before BLE to avoid fragmenting scarce internal SRAM. */
    vokrr::network_start();
    /* Start BLE advertising at boot so iPhone can discover the watch immediately. */
    esp_brookesia::apps::va_init_ble();
    ESP_LOGI("Vokrr", "Runtime ready (internal heap %u bytes, largest %u; PSRAM %u bytes)",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    /* Initialize physical buttons: BOOT and PWR */
    ESP_UTILS_CHECK_ERROR_EXIT(buttons_init(), "Buttons init failed");

    /* PWR: short press -> dashboard */
    buttons_register_pwr_cb([](void *user_data) {
        (void)user_data;
        LvLockGuard gui_guard;
        vokrr::os_show_page(0);
    }, nullptr);

    /* PWR: long press -> Jarvis page */
    buttons_register_pwr_long_cb([](void *user_data) {
        (void)user_data;
        LvLockGuard gui_guard;
        vokrr::os_show_page(3);
    }, nullptr);

    /* BOOT: short press -> show away screen */
    buttons_register_boot_short_cb([](void *user_data) {
        (void)user_data;
        LvLockGuard gui_guard;
        vokrr::os_show_page(5);
    }, NULL);

    /* BOOT: long press -> restart the watch */
    buttons_register_boot_long_cb([](void *user_data) {
        (void)user_data;
        ESP_UTILS_LOGI("BOOT long press: restarting watch");
        esp_restart();
    }, NULL);

    /* Wake callback: dismiss away screen on button press while in away mode */
    buttons_register_wake_cb([](void *user_data) {
        (void)user_data;
        LvLockGuard gui_guard;
        vokrr::os_show_page(0);
    }, NULL);

}
