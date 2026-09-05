#pragma once
#include <stdint.h>

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

// --- Hardware Pin Definitions ---
#if defined(TONEX_BOARD_P4_WIFI6)
// The Waveshare P4 WiFi6 board has no user-addressable RGB status LED.
#define PIN_RGB_LED       -1
#else
#define PIN_RGB_LED       48
#endif
#define PIN_USB_DM        19
#define PIN_USB_DP        20

// --- Network & Server Configuration ---
#ifndef TONEX_WIFI_SSID
#define TONEX_WIFI_SSID   ""
#endif

#ifndef TONEX_WIFI_PASS
#define TONEX_WIFI_PASS   ""
#endif

#ifndef TONEX_MDNS_NAME
#define TONEX_MDNS_NAME   "tonex"
#endif

#ifndef TONEX_WIFI_CONNECT_TIMEOUT_MS
// ESP-Hosted can take longer than a direct ESP Wi-Fi connection to complete
// association and DHCP. Do not start the recovery AP while that exchange is
// still in progress.
#define TONEX_WIFI_CONNECT_TIMEOUT_MS 60000
#endif

#ifndef TONEX_WIFI_RECONNECT_INTERVAL_MS
#define TONEX_WIFI_RECONNECT_INTERVAL_MS 10000
#endif

#ifndef TONEX_SETUP_AP_PREFIX
#define TONEX_SETUP_AP_PREFIX "TONEX-Setup"
#endif

#ifndef TONEX_SETUP_AP_PASSWORD
#define TONEX_SETUP_AP_PASSWORD "tonexsetup"
#endif

#define HTTP_PORT         80
#define WS_PATH           "/ws"

// --- ToneX Constants ---
#define TONEX_TOTAL_BANKS 50
#define TONEX_SLOTS_COUNT 3
#define TONEX_TOTAL_PRESETS (TONEX_TOTAL_BANKS * TONEX_SLOTS_COUNT) // 150
#define TONEX_USB_VID     0x1963 // IK Multimedia
