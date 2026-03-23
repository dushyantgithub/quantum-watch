/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Voice Assistant app for quantum-watch: BLE audio streaming to iPhone companion.
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class VoiceAssistantApp : public systems::phone::App {
public:
    static VoiceAssistantApp *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~VoiceAssistantApp() override;

protected:
    VoiceAssistantApp(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static VoiceAssistantApp *_instance;
};

} // namespace esp_brookesia::apps

/* Initialize BLE GATT service at boot (before app is opened) */
namespace esp_brookesia::apps {
    bool va_init_ble(void);
    bool va_ble_is_connected(void);
    bool va_request_health_refresh(void);
}
