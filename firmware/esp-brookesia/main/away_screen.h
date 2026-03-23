/*
 * SPDX-FileCopyrightText: 2025 Dushyant Singh
 *
 * SPDX-License-Identifier: MIT
 *
 * Away screen (always-on watch face) for quantum-watch.
 * Shows time, date, and day on a black background.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void away_screen_init(void);
void away_screen_show(void);
void away_screen_hide(void);
bool away_screen_is_active(void);
void away_screen_set_weather(int temp_c, const char *location);

#ifdef __cplusplus
}
#endif
