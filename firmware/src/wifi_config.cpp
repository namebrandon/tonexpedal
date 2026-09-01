#include "wifi_config.h"

#include <cctype>

static WifiValidationResult valid() {
    return {WifiValidationCode::OK, "", ""};
}

static WifiValidationResult invalid(
    WifiValidationCode code,
    const char* field,
    const char* message
) {
    return {code, field, message};
}

WifiValidationResult validateWifiSettings(const WifiSettings& settings) {
    if (settings.ssid.empty()) {
        return invalid(WifiValidationCode::SSID_REQUIRED, "ssid", "Network name is required");
    }
    if (settings.ssid.size() > 32) {
        return invalid(WifiValidationCode::SSID_TOO_LONG, "ssid", "Network name must be 32 bytes or fewer");
    }
    for (unsigned char character : settings.ssid) {
        if (character < 0x20 || character == 0x7F) {
            return invalid(
                WifiValidationCode::SSID_CONTROL_CHARACTER,
                "ssid",
                "Network name cannot contain control characters"
            );
        }
    }

    if (settings.openNetwork) {
        if (!settings.password.empty()) {
            return invalid(
                WifiValidationCode::OPEN_NETWORK_PASSWORD,
                "password",
                "An open network must not include a password"
            );
        }
    } else if (settings.password.size() < 8) {
        return invalid(
            WifiValidationCode::PASSWORD_REQUIRED,
            "password",
            "Wi-Fi password must contain at least 8 bytes"
        );
    } else if (settings.password.size() > 63) {
        return invalid(
            WifiValidationCode::PASSWORD_TOO_LONG,
            "password",
            "Wi-Fi password must contain no more than 63 bytes"
        );
    } else {
        for (unsigned char character : settings.password) {
            if (character < 0x20 || character == 0x7F) {
                return invalid(
                    WifiValidationCode::PASSWORD_CONTROL_CHARACTER,
                    "password",
                    "Wi-Fi password cannot contain control characters"
                );
            }
        }
    }

    if (settings.hostname.empty()) {
        return invalid(WifiValidationCode::HOSTNAME_REQUIRED, "hostname", "Device name is required");
    }
    if (settings.hostname.size() > 63) {
        return invalid(WifiValidationCode::HOSTNAME_TOO_LONG, "hostname", "Device name must be 63 characters or fewer");
    }
    if (settings.hostname.front() == '-' || settings.hostname.back() == '-') {
        return invalid(
            WifiValidationCode::HOSTNAME_INVALID,
            "hostname",
            "Device name cannot begin or end with a hyphen"
        );
    }
    for (unsigned char character : settings.hostname) {
        if (!(std::isalnum(character) || character == '-')) {
            return invalid(
                WifiValidationCode::HOSTNAME_INVALID,
                "hostname",
                "Device name may contain only letters, numbers, and hyphens"
            );
        }
    }

    return valid();
}
