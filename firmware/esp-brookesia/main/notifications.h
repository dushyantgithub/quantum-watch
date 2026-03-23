/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Notification system for quantum-watch.
 * Handles incoming call overlay, beep sound, and notification drawer.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void notifications_init(void);

/* Called from BLE when iPhone sends notification data */
void notifications_call_incoming(void);
void notifications_call_ended(void);
void notifications_call_missed(void);

/* Generic notification API (used by ANCS client) */
void notifications_add(const char *app, const char *title,
                       const char *message, uint8_t category,
                       uint32_t uid);
void notifications_remove(uint32_t uid);

/* Notification drawer */
bool notifications_drawer_is_open(void);

#ifdef __cplusplus
}
#endif
