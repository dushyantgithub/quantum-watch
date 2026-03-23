/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class HealthApp : public systems::phone::App {
public:
    static HealthApp *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~HealthApp() override;

protected:
    HealthApp(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool resume(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static HealthApp *_instance;
};

void health_app_on_ble_data_chunk(const uint8_t *data, uint16_t len, bool final);

} // namespace esp_brookesia::apps
