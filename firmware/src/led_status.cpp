#include "led_status.h"
#include "config.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#else
static uint32_t millis() { return 0; }
#endif

LedStatus StatusLed;

LedStatus::LedStatus() : _state(LedState::OFF), _lastBlink(0), _blinkOn(false) {}

void LedStatus::begin() {
#ifndef NATIVE_TEST
    pinMode(PIN_RGB_LED, OUTPUT);
    setState(LedState::WIFI_CONNECTING);
#endif
}

void LedStatus::setState(LedState state) {
    _state = state;
    _blinkOn = true;
    _lastBlink = millis();
}

void LedStatus::update() {
#ifndef NATIVE_TEST
    uint32_t now = millis();

    switch (_state) {
        case LedState::WIFI_CONNECTING:
            if (now - _lastBlink >= 300) {
                _lastBlink = now;
                _blinkOn = !_blinkOn;
                setColor(0, 0, _blinkOn ? 64 : 0); // Blue blink
            }
            break;

        case LedState::WIFI_CONNECTED:
            setColor(0, 0, 64); // Solid Blue
            break;

        case LedState::TONEX_CONNECTED:
            setColor(0, 64, 0); // Solid Green
            break;

        case LedState::SYNCING:
            if (now - _lastBlink >= 80) {
                _lastBlink = now;
                _blinkOn = !_blinkOn;
                setColor(_blinkOn ? 64 : 0, 0, _blinkOn ? 64 : 0); // Purple strobe
            }
            break;

        case LedState::ERROR_STATE:
            if (now - _lastBlink >= 200) {
                _lastBlink = now;
                _blinkOn = !_blinkOn;
                setColor(_blinkOn ? 64 : 0, 0, 0); // Red blink
            }
            break;

        case LedState::OFF:
        default:
            setColor(0, 0, 0);
            break;
    }
#endif
}

void LedStatus::setColor(uint8_t r, uint8_t g, uint8_t b) {
#ifndef NATIVE_TEST
    neopixelWrite(PIN_RGB_LED, r, g, b);
#endif
}
