#include <unity.h>
#include "wifi_config.h"

void setUp(void) {}
void tearDown(void) {}

static WifiSettings validSettings() {
    return {"Studio-WLAN", "correct-horse", "tonex-stage", false};
}

void test_accepts_secured_and_explicit_open_networks(void) {
    TEST_ASSERT_TRUE(validateWifiSettings(validSettings()).ok());

    WifiSettings open = {"Guest", "", "tonex", true};
    TEST_ASSERT_TRUE(validateWifiSettings(open).ok());
}

void test_rejects_invalid_ssid_lengths_and_controls(void) {
    WifiSettings settings = validSettings();
    settings.ssid.clear();
    TEST_ASSERT_EQUAL(
        static_cast<int>(WifiValidationCode::SSID_REQUIRED),
        static_cast<int>(validateWifiSettings(settings).code)
    );

    settings = validSettings();
    settings.ssid = std::string(33, 'a');
    TEST_ASSERT_EQUAL(
        static_cast<int>(WifiValidationCode::SSID_TOO_LONG),
        static_cast<int>(validateWifiSettings(settings).code)
    );

    settings = validSettings();
    settings.ssid = "Studio\nWLAN";
    TEST_ASSERT_EQUAL(
        static_cast<int>(WifiValidationCode::SSID_CONTROL_CHARACTER),
        static_cast<int>(validateWifiSettings(settings).code)
    );
}

void test_rejects_password_mistakes(void) {
    WifiSettings settings = validSettings();
    settings.password = "short";
    TEST_ASSERT_EQUAL(
        static_cast<int>(WifiValidationCode::PASSWORD_REQUIRED),
        static_cast<int>(validateWifiSettings(settings).code)
    );

    settings = validSettings();
    settings.password = std::string(64, 'a');
    TEST_ASSERT_EQUAL(
        static_cast<int>(WifiValidationCode::PASSWORD_TOO_LONG),
        static_cast<int>(validateWifiSettings(settings).code)
    );

    settings = validSettings();
    settings.password = "contains\nnewline";
    TEST_ASSERT_EQUAL(
        static_cast<int>(WifiValidationCode::PASSWORD_CONTROL_CHARACTER),
        static_cast<int>(validateWifiSettings(settings).code)
    );

    settings = validSettings();
    settings.openNetwork = true;
    TEST_ASSERT_EQUAL(
        static_cast<int>(WifiValidationCode::OPEN_NETWORK_PASSWORD),
        static_cast<int>(validateWifiSettings(settings).code)
    );
}

void test_rejects_unsafe_hostnames(void) {
    for (const std::string& hostname : {std::string(), std::string("-tonex"), std::string("tonex-"), std::string("tone_x")}) {
        WifiSettings settings = validSettings();
        settings.hostname = hostname;
        TEST_ASSERT_FALSE(validateWifiSettings(settings).ok());
    }

    WifiSettings settings = validSettings();
    settings.hostname = std::string(64, 'a');
    TEST_ASSERT_EQUAL(
        static_cast<int>(WifiValidationCode::HOSTNAME_TOO_LONG),
        static_cast<int>(validateWifiSettings(settings).code)
    );
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_accepts_secured_and_explicit_open_networks);
    RUN_TEST(test_rejects_invalid_ssid_lengths_and_controls);
    RUN_TEST(test_rejects_password_mistakes);
    RUN_TEST(test_rejects_unsafe_hostnames);
    return UNITY_END();
}
