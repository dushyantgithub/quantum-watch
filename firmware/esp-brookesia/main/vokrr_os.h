/*
 * SPDX-FileCopyrightText: 2026 Dushyant Singh
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

namespace vokrr {

void os_init();
void os_show_page(uint8_t page, bool animated = true);
void os_on_assistant_text(const char *text);
void os_on_chat_exchange(const char *user, const char *assistant);
void os_set_listening(bool listening, bool processing = false);
void os_set_context(long unix_timestamp, int temperature_c, const char *location);

}  // namespace vokrr
