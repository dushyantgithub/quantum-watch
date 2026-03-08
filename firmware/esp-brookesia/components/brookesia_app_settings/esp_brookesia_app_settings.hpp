/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Settings app for quantum-watch: Display, Sound, WiFi, Bluetooth, Battery.
 * Apple Watch-style navigation: main list -> detail screens.
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

#ifdef __cplusplus
extern "C" {
#endif
void settings_wifi_boot_connect(void);
#ifdef __cplusplus
}
#endif

namespace esp_brookesia::apps {

class SettingsApp : public systems::phone::App {
public:
    static SettingsApp *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~SettingsApp() override;

protected:
    SettingsApp(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static SettingsApp *_instance;
};

} // namespace esp_brookesia::apps
