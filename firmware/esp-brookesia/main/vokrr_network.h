/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace vokrr {

void network_start();
void network_request_refresh();
bool network_toggle_device(const char *device_id, bool desired_state);

}  // namespace vokrr
