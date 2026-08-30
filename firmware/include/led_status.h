#pragma once

#include <stdint.h>

enum class LedState {
    OFF,
    WIFI_CONNECTING, // Blinking Blue
    WIFI_CONNECTED,  // Solid Blue
    TONEX_CONNECTED, // Solid Green
    SYNCING,         // Fast Pulsing Purple
    ERROR_STATE      // Blinking Red
};

class LedStatus {
public:
    LedStatus();
    void begin();
    void setState(LedState state);
    void update();

private:
    LedState _state;
    uint32_t _lastBlink;
    bool _blinkOn;
    void setColor(uint8_t r, uint8_t g, uint8_t b);
};

extern LedStatus StatusLed;
