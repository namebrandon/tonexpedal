#pragma once

#include <string>

enum class WifiValidationCode {
    OK,
    SSID_REQUIRED,
    SSID_TOO_LONG,
    SSID_CONTROL_CHARACTER,
    PASSWORD_REQUIRED,
    PASSWORD_TOO_LONG,
    PASSWORD_CONTROL_CHARACTER,
    OPEN_NETWORK_PASSWORD,
    HOSTNAME_REQUIRED,
    HOSTNAME_TOO_LONG,
    HOSTNAME_INVALID
};

struct WifiSettings {
    std::string ssid;
    std::string password;
    std::string hostname;
    bool openNetwork = false;
};

struct WifiValidationResult {
    WifiValidationCode code;
    const char* field;
    const char* message;

    bool ok() const { return code == WifiValidationCode::OK; }
};

WifiValidationResult validateWifiSettings(const WifiSettings& settings);
