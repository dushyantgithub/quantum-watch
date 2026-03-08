/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "boost/thread.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"
#include "./dark/stylesheet.hpp"
#include "buttons.h"
#include "away_screen.h"
#include "esp_sleep.h"
#include "esp_brookesia_app_settings.hpp"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;

#define LVGL_PORT_INIT_CONFIG() \
    {                               \
        .task_priority = 4,       \
        .task_stack = 10 * 1024,       \
        .task_affinity = -1,      \
        .task_max_sleep_ms = 500, \
        .timer_period_ms = 5,     \
    }

#define AWAY_TIMEOUT_MS 120000

constexpr bool EXAMPLE_SHOW_MEM_INFO = false;

extern "C" void app_main(void)
{
    ESP_UTILS_LOGI("Display ESP-Brookesia phone demo");

    /* Auto-connect WiFi and set IST timezone before display init */
    settings_wifi_boot_connect();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = LVGL_PORT_INIT_CONFIG(),
    };
    ESP_UTILS_CHECK_NULL_EXIT(bsp_display_start_with_config(&cfg), "Start display failed");
    ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Turn on display backlight failed");

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

    /* Create a phone object */
    Phone *phone = new (std::nothrow) Phone();
    ESP_UTILS_CHECK_NULL_EXIT(phone, "Create phone failed");

    /* Try using a stylesheet that corresponds to the resolution */
    if ((BSP_LCD_H_RES == 410) && (BSP_LCD_V_RES == 502)) {
        Stylesheet *stylesheet = new (std::nothrow) Stylesheet(STYLESHEET_410_502_DARK);
        ESP_UTILS_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");

        ESP_UTILS_LOGI("Using stylesheet (%s)", stylesheet->core.name);
        ESP_UTILS_CHECK_FALSE_EXIT(phone->addStylesheet(stylesheet), "Add stylesheet failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->activateStylesheet(stylesheet), "Activate stylesheet failed");
        delete stylesheet;
    }

    {
        // When operating on non-GUI tasks, should acquire a lock before operating on LVGL
        LvLockGuard gui_guard;

        /* Begin the phone */
        ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");
        // assert(phone->getDisplay().showContainerBorder() && "Show container border failed");

        /* Init and install apps from registry */
        std::vector<systems::base::Manager::RegistryAppInfo> inited_apps;
        ESP_UTILS_CHECK_FALSE_EXIT(phone->initAppFromRegistry(inited_apps), "Init app registry failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installAppFromRegistry(inited_apps), "Install app registry failed");

        /* Apply a subtle dark gradient on the homescreen background.
         * Uses LVGL's native gradient rendering (zero image bytes in flash). */
        {
            lv_obj_t *main_obj = lv_obj_get_child(lv_screen_active(), 0);
            if (main_obj) {
                lv_obj_set_style_bg_color(main_obj, lv_color_hex(0x121C30), 0);
                lv_obj_set_style_bg_grad_color(main_obj, lv_color_hex(0x1A1A1A), 0);
                lv_obj_set_style_bg_grad_dir(main_obj, LV_GRAD_DIR_VER, 0);
                lv_obj_set_style_bg_opa(main_obj, LV_OPA_COVER, 0);
            }
        }

        /* Create a timer to update the clock */
        lv_timer_create([](lv_timer_t *t) {
            time_t now;
            struct tm timeinfo;
            Phone *phone = (Phone *)t->user_data;

            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

            time(&now);
            localtime_r(&now, &timeinfo);

            ESP_UTILS_CHECK_FALSE_EXIT(
                phone->getDisplay().getStatusBar()->setClock(timeinfo.tm_hour, timeinfo.tm_min),
                "Refresh status bar failed"
            );
        }, 1000, phone);

        /* Initialize the away screen overlay */
        away_screen_init();

        /* Inactivity timer: show away screen after AWAY_TIMEOUT_MS of no interaction */
        lv_timer_create([](lv_timer_t *t) {
            (void)t;
            if (!away_screen_is_active()) {
                uint32_t inactive = lv_display_get_inactive_time(NULL);
                if (inactive > AWAY_TIMEOUT_MS) {
                    away_screen_show();
                    buttons_set_away_mode(true);
                }
            }
        }, 2000, NULL);
    }

    /* Initialize physical buttons: BOOT and PWR */
    ESP_UTILS_CHECK_ERROR_EXIT(buttons_init(), "Buttons init failed");

    /* PWR: short press -> navigate to home screen */
    buttons_register_pwr_cb([](void *user_data) {
        Phone *p = (Phone *)user_data;
        if (p) {
            LvLockGuard gui_guard;
            p->sendNavigateEvent(systems::base::Manager::NavigateType::HOME);
        }
    }, phone);

    /* BOOT: short press -> toggle screen on/off */
    buttons_register_boot_short_cb([](void *user_data) {
        (void)user_data;
        int b = bsp_display_brightness_get();
        if (b > 0) {
            bsp_display_backlight_off();
        } else {
            bsp_display_backlight_on();
        }
    }, NULL);

    /* BOOT: long press -> shutdown (deep sleep). Wake by pressing BOOT again. */
    buttons_register_boot_long_cb([](void *user_data) {
        (void)user_data;
        ESP_UTILS_LOGI("BOOT long press: entering deep sleep (shutdown)");
        esp_sleep_enable_ext0_wakeup(BUTTON_BOOT_GPIO, 0);
        esp_deep_sleep_start();
    }, NULL);

    /* Wake callback: dismiss away screen on button press while in away mode */
    buttons_register_wake_cb([](void *user_data) {
        (void)user_data;
        LvLockGuard gui_guard;
        away_screen_hide();
        buttons_set_away_mode(false);
    }, NULL);

    if constexpr (EXAMPLE_SHOW_MEM_INFO) {
        esp_utils::thread_config_guard thread_config({
            .name = "mem_info",
            .stack_size = 4096,
        });
        boost::thread([ = ]() {
            char buffer[128];    /* Make sure buffer is enough for `sprintf` */
            size_t internal_free = 0;
            size_t internal_total = 0;
            size_t external_free = 0;
            size_t external_total = 0;

            while (1) {
                internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
                internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
                external_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
                external_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
                sprintf(buffer,
                        "\t           Biggest /     Free /    Total\n"
                        "\t  SRAM : [%8d / %8d / %8d]\n"
                        "\t PSRAM : [%8d / %8d / %8d]",
                        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), internal_free, internal_total,
                        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), external_free, external_total);
                ESP_UTILS_LOGI("\n%s", buffer);

                {
                    LvLockGuard gui_guard;
                    ESP_UTILS_CHECK_FALSE_EXIT(
                        phone->getDisplay().getRecentsScreen()->setMemoryLabel(
                            internal_free / 1024, internal_total / 1024, external_free / 1024, external_total / 1024
                        ), "Set memory label failed"
                    );
                }

                boost::this_thread::sleep_for(boost::chrono::seconds(5));
            }
        }).detach();
    }
}
