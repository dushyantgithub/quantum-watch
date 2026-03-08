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

#ifdef __cplusplus
}
#endif
