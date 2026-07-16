#pragma once

#if __has_include("vokrr_secrets.local.h")
#include "vokrr_secrets.local.h"
#else
#include "vokrr_secrets.example.h"
#endif

#define VOKRR_OS_NAME "VokrrOS"
#define VOKRR_OS_VERSION "1.0.0"
#define VOKRR_DEVICE_NAME "Vokrr Watch"
#define VOKRR_TIMEZONE "IST-5:30"

#define VOKRR_WIFI_CONNECT_TIMEOUT_MS 15000
#define VOKRR_ROOM_REFRESH_MS 30000
#define VOKRR_HEALTH_REFRESH_MS 300000
#define VOKRR_CACHE_SAVE_MS 300000
#define VOKRR_INACTIVITY_TIMEOUT_MS 30000
#define VOKRR_ACTIVE_BRIGHTNESS 76
#define VOKRR_AOD_BRIGHTNESS 10
