#pragma once
#include <stdint.h>

// --- Hardware Pin Definitions ---
#define PIN_RGB_LED       48
#define PIN_USB_DM        19
#define PIN_USB_DP        20

// --- Network & Server Configuration ---
#ifndef TONEX_MDNS_NAME
#define TONEX_MDNS_NAME   "tonex"
#endif

#ifndef TONEX_AP_SSID
#define TONEX_AP_SSID     "ToneX-Remote"
#endif

#ifndef TONEX_AP_PASS
#define TONEX_AP_PASS     ""  // Open AP by default for instant connection
#endif

#define HTTP_PORT         80
#define WS_PATH           "/ws"

// --- ToneX Constants ---
#define TONEX_TOTAL_BANKS 50
#define TONEX_SLOTS_COUNT 3
#define TONEX_TOTAL_PRESETS (TONEX_TOTAL_BANKS * TONEX_SLOTS_COUNT) // 150
#define TONEX_USB_VID     0x1963 // IK Multimedia
