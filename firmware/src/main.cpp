#ifndef NATIVE_TEST

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "led_status.h"
#include "tonex_usb_host.h"
#include "wifi_config.h"
#include "ws_bridge.h"

AsyncWebServer server(HTTP_PORT);

static constexpr const char* WIFI_NVS_NAMESPACE = "tonex-wifi";
static bool webServerStarted = false;
static bool filesystemMounted = false;
static bool mdnsStarted = false;
static bool wifiWasConnected = false;
static bool wifiSetupMode = false;
static bool wifiScanActive = false;
static bool wifiSettingsFromNvs = false;
static int wifiLastDisconnectReason = 0;
static int wifiLastScanResult = WIFI_SCAN_FAILED;
static uint32_t wifiScanStartedMs = 0;
static uint32_t restartAtMs = 0;
static String setupAccessPointSsid;
static WifiSettings wifiSettings;

void setupWebServer();

static void clearWifiScan() {
    WiFi.scanDelete();
    wifiScanActive = false;
}

static bool startWifiScan() {
    if (wifiScanActive) return true;

    // ESP-Hosted's factory C6 firmware can complete a scan before an HTTP client
    // has issued its first polling request. Run this bounded scan in the request
    // instead, so the result is captured before the browser asks for it.
    WiFi.scanDelete();
    WiFi.setScanTimeout(15000);
    wifiLastScanResult = WIFI_SCAN_FAILED;
    // An active scan gives the best chance of finding ordinary, non-hidden 2.4 GHz
    // networks. This bridge's C6 radio is a 2.4 GHz radio; 5 GHz-only networks
    // cannot be listed or joined.
    wifiScanActive = true;
    wifiScanStartedMs = millis();
    wifiLastScanResult = WiFi.scanNetworks(false, true, false, 120);
    wifiScanActive = false;

    const uint32_t elapsedMs = millis() - wifiScanStartedMs;
    if (wifiLastScanResult < 0) {
        Serial.printf("[WIFI] Setup-network scan failed after %lu ms (result %d).\n",
                      static_cast<unsigned long>(elapsedMs), wifiLastScanResult);
        return false;
    }

    Serial.printf(
        "[WIFI] Setup-network scan found %d network(s) in %lu ms.\n",
        wifiLastScanResult,
        static_cast<unsigned long>(elapsedMs)
    );
    return true;
}

static const char* wifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD: return "no_shield";
        case WL_IDLE_STATUS: return "idle";
        case WL_NO_SSID_AVAIL: return "ssid_not_found";
        case WL_SCAN_COMPLETED: return "scan_completed";
        case WL_CONNECTED: return "connected";
        case WL_CONNECT_FAILED: return "connect_failed";
        case WL_CONNECTION_LOST: return "connection_lost";
        case WL_DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}

static void logWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        wifiLastDisconnectReason = info.wifi_sta_disconnected.reason;
        const auto reason = static_cast<wifi_err_reason_t>(wifiLastDisconnectReason);
        Serial.printf(
            "[WIFI] Station disconnect reason %d (%s)\n",
            wifiLastDisconnectReason,
            WiFi.disconnectReasonName(reason)
        );
    } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
        wifiLastDisconnectReason = 0;
        Serial.println("[WIFI] Station associated; waiting for an IP address.");
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        Serial.println("[WIFI] Station received an IP address.");
    } else if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {
        Serial.println("[WIFI] Setup-network scan completed.");
    }
}

static void sendJson(AsyncWebServerRequest* request, int status, const JsonDocument& doc) {
    String payload;
    serializeJson(doc, payload);
    request->send(status, "application/json", payload);
}

static void appendScanNetworks(JsonDocument& doc) {
    JsonArray networks = doc["networks"].to<JsonArray>();
    constexpr int MAX_SETUP_NETWORKS = 24;
    for (int index = 0; index < wifiLastScanResult && networks.size() < MAX_SETUP_NETWORKS; index++) {
        const String ssid = WiFi.SSID(index);
        if (ssid.isEmpty()) continue; // Hidden networks can still be entered manually.

        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = ssid;
        network["rssi"] = WiFi.RSSI(index);
        network["secure"] = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
        network["channel"] = WiFi.channel(index);
    }
    Serial.printf(
        "[WIFI] Returning %u visible network(s) from %d scan record(s).\n",
        static_cast<unsigned>(networks.size()),
        wifiLastScanResult
    );
}

static bool loadWifiSettings() {
    Preferences preferences;
    if (preferences.begin(WIFI_NVS_NAMESPACE, true)) {
        const String storedSsid = preferences.getString("ssid", "");
        if (!storedSsid.isEmpty()) {
            wifiSettings.ssid = storedSsid.c_str();
            wifiSettings.password = preferences.getString("pass", "").c_str();
            wifiSettings.hostname = preferences.getString("host", TONEX_MDNS_NAME).c_str();
            wifiSettings.openNetwork = preferences.getBool("open", false);
            wifiSettingsFromNvs = true;
        }
        preferences.end();
    }

    if (!wifiSettingsFromNvs && TONEX_WIFI_SSID[0] != '\0') {
        wifiSettings.ssid = TONEX_WIFI_SSID;
        wifiSettings.password = TONEX_WIFI_PASS;
        wifiSettings.hostname = TONEX_MDNS_NAME;
        wifiSettings.openNetwork = TONEX_WIFI_PASS[0] == '\0';
    }

    if (wifiSettings.hostname.empty()) wifiSettings.hostname = TONEX_MDNS_NAME;
    return !wifiSettings.ssid.empty() && validateWifiSettings(wifiSettings).ok();
}

static bool saveWifiSettings(const WifiSettings& settings) {
    Preferences preferences;
    if (!preferences.begin(WIFI_NVS_NAMESPACE, false)) return false;
    preferences.putString("ssid", settings.ssid.c_str());
    preferences.putString("pass", settings.password.c_str());
    preferences.putString("host", settings.hostname.c_str());
    preferences.putBool("open", settings.openNetwork);
    preferences.end();
    wifiSettings = settings;
    wifiSettingsFromNvs = true;
    return true;
}

static bool clearWifiSettings() {
    Preferences preferences;
    if (!preferences.begin(WIFI_NVS_NAMESPACE, false)) return false;
    const bool cleared = preferences.clear();
    preferences.end();
    return cleared;
}

static String makeSetupAccessPointSsid() {
    const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFF);
    char name[33];
    snprintf(name, sizeof(name), "%s-%06lX", TONEX_SETUP_AP_PREFIX, static_cast<unsigned long>(suffix));
    return String(name);
}

static void startSetupAccessPoint(const char* reason) {
    if (wifiSetupMode) return;

    wifiSetupMode = true;
    setupAccessPointSsid = makeSetupAccessPointSsid();
    // Setup mode must be able to scan even if the previously saved network has
    // disappeared. Keep the station interface available for scanning, but stop
    // Arduino from repeatedly attempting that stale connection in the background.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    const bool started = WiFi.softAP(setupAccessPointSsid.c_str(), TONEX_SETUP_AP_PASSWORD);

    Serial.printf("[WIFI] Setup mode: %s\n", reason);
    if (started) {
        Serial.printf(
            "[WIFI] Join %s and open http://%s\n",
            setupAccessPointSsid.c_str(),
            WiFi.softAPIP().toString().c_str()
        );
    } else {
        Serial.println("[WIFI] Could not start setup access point");
    }

    setupWebServer();
    StatusLed.setState(started ? LedState::WIFI_CONNECTED : LedState::ERROR_STATE);
}

static void stopSetupAccessPoint() {
    if (!wifiSetupMode) return;
    clearWifiScan();
    WiFi.softAPdisconnect(true);
    wifiSetupMode = false;
    setupAccessPointSsid = "";
    Serial.println("[WIFI] Setup access point stopped; configuration API locked");
}

static void startNetworkServices() {
    stopSetupAccessPoint();

    // Reassert this after association/DHCP as well. Some radio firmware resets
    // its power-save policy while it establishes a station connection.
    if (!WiFi.setSleep(false)) {
        Serial.println("[WIFI] Warning: could not disable connected-station power saving.");
    }
    wifi_ps_type_t powerSaveMode = WIFI_PS_NONE;
    if (esp_wifi_get_ps(&powerSaveMode) == ESP_OK) {
        Serial.printf("[WIFI] Station power-save mode: %d (0 means disabled).\n", powerSaveMode);
    } else {
        Serial.println("[WIFI] Warning: could not read station power-save mode.");
    }

    const IPAddress address = WiFi.localIP();
    Serial.print("[WIFI] Connected. IP address: ");
    Serial.println(address);

    if (!mdnsStarted && MDNS.begin(wifiSettings.hostname.c_str())) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        mdnsStarted = true;
        Serial.printf("[MDNS] Responder started: http://%s.local\n", wifiSettings.hostname.c_str());
    }

    setupWebServer();
    StatusLed.setState(ToneX.isConnected() ? LedState::TONEX_CONNECTED : LedState::WIFI_CONNECTED);
}

bool setupWiFi() {
    StatusLed.setState(LedState::WIFI_CONNECTING);

    if (!loadWifiSettings()) {
        Serial.println("[WIFI] No valid station credentials configured.");
        startSetupAccessPoint("station credentials are missing or invalid");
        return false;
    }

    Serial.printf("[WIFI] Connecting to station network: %s\n", wifiSettings.ssid.c_str());
    WiFi.mode(WIFI_STA);
    // This bridge carries interactive MIDI control traffic and is externally
    // powered. Avoid modem sleep, which can defer inbound packets until a DTIM
    // wake interval and produce highly variable control latency.
    if (!WiFi.setSleep(false)) {
        Serial.println("[WIFI] Warning: could not disable station power saving.");
    }
    WiFi.setHostname(wifiSettings.hostname.c_str());
    WiFi.setAutoReconnect(true);
    WiFi.begin(
        wifiSettings.ssid.c_str(),
        wifiSettings.openNetwork ? nullptr : wifiSettings.password.c_str()
    );

    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < TONEX_WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    wifiWasConnected = WiFi.status() == WL_CONNECTED;
    if (!wifiWasConnected) {
        Serial.println("[WIFI] Initial station connection timed out; setup recovery is available while reconnecting.");
        startSetupAccessPoint("station connection timed out");
        return false;
    }

    startNetworkServices();
    return true;
}

void setupWebServer() {
    if (webServerStarted) return;

    if (!filesystemMounted) {
        // The 16 MB partition table names this SPIFFS-subtype partition "littlefs".
        // Arduino defaults to the partition label "spiffs", so pass the configured
        // label explicitly or the web assets cannot be mounted after boot.
        filesystemMounted = LittleFS.begin(true, "/littlefs", 10, "littlefs");
        Serial.println(filesystemMounted ? "[FS] LittleFS mounted successfully" : "[FS] Error mounting LittleFS");
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        const char* page = wifiSetupMode ? "/setup.html" : "/index.html";
        request->send(LittleFS, page, "text/html");
    });
    server.serveStatic("/index.html", LittleFS, "/index.html");
    server.serveStatic("/setup.html", LittleFS, "/setup.html");
    server.serveStatic("/ui.css", LittleFS, "/ui.css");
    server.serveStatic("/setup.css", LittleFS, "/setup.css");
    server.serveStatic("/favicon.svg", LittleFS, "/favicon.svg");

    server.on("/api/bridge", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["service"] = "tonex-bridge";
        doc["protocol_version"] = 1;
        doc["setup_mode"] = wifiSetupMode;
        sendJson(request, 200, doc);
    });

    server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        const wl_status_t stationStatus = WiFi.status();
        doc["setup_mode"] = wifiSetupMode;
        doc["configured"] = !wifiSettings.ssid.empty();
        doc["stored"] = wifiSettingsFromNvs;
        doc["ssid"] = wifiSettings.ssid;
        doc["hostname"] = wifiSettings.hostname;
        doc["open_network"] = wifiSettings.openNetwork;
        doc["station_connected"] = stationStatus == WL_CONNECTED;
        doc["station_status"] = wifiStatusName(stationStatus);
        doc["station_status_code"] = stationStatus;
        doc["station_mac"] = WiFi.macAddress();
        if (wifiLastDisconnectReason > 0) {
            const auto reason = static_cast<wifi_err_reason_t>(wifiLastDisconnectReason);
            doc["last_disconnect_reason"] = wifiLastDisconnectReason;
            doc["last_disconnect"] = WiFi.disconnectReasonName(reason);
        }
        if (stationStatus == WL_CONNECTED) {
            doc["station_ip"] = WiFi.localIP().toString();
            doc["station_rssi"] = WiFi.RSSI();
        }
        if (wifiSetupMode) {
            doc["setup_ssid"] = setupAccessPointSsid;
            doc["setup_ip"] = WiFi.softAPIP().toString();
            doc["setup_mac"] = WiFi.softAPmacAddress();
        }
        sendJson(request, 200, doc);
    });

    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!wifiSetupMode) {
            request->send(403, "application/json", "{\"error\":\"setup_required\"}");
            return;
        }

        JsonDocument doc;
        doc["scanning"] = wifiScanActive;

        if (!wifiScanActive && wifiLastScanResult < 0) {
            doc["error"] = "scan_failed";
            doc["message"] = "The Wi-Fi radio did not complete its scan. Try again from the setup network.";
        } else if (!wifiScanActive && wifiLastScanResult >= 0) {
            appendScanNetworks(doc);
        }

        sendJson(request, 200, doc);
    });

    server.on("/api/wifi/scan", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!wifiSetupMode) {
            request->send(403, "application/json", "{\"error\":\"setup_required\"}");
            return;
        }

        if (!startWifiScan()) {
            request->send(503, "application/json", "{\"error\":\"scan_unavailable\"}");
            return;
        }

        JsonDocument doc;
        doc["scanning"] = false;
        appendScanNetworks(doc);
        sendJson(request, 200, doc);
    });

    AsyncCallbackJsonWebHandler* wifiHandler = new AsyncCallbackJsonWebHandler(
        "/api/wifi",
        [](AsyncWebServerRequest* request, JsonVariant& json) {
            const uint64_t requestId = json["request_id"] | uint64_t(0);
            if (!wifiSetupMode) {
                JsonDocument error;
                error["error"] = "setup_required";
                error["message"] = "Wi-Fi changes are available only in setup mode";
                if (requestId > 0) error["request_id"] = requestId;
                sendJson(request, 403, error);
                return;
            }

            WifiSettings candidate;
            candidate.ssid = json["ssid"] | "";
            candidate.password = json["password"] | "";
            candidate.hostname = json["hostname"] | TONEX_MDNS_NAME;
            candidate.openNetwork = json["open_network"] | false;

            const WifiValidationResult validation = validateWifiSettings(candidate);
            if (!validation.ok()) {
                JsonDocument error;
                error["error"] = "validation_failed";
                error["field"] = validation.field;
                error["message"] = validation.message;
                if (requestId > 0) error["request_id"] = requestId;
                sendJson(request, 400, error);
                return;
            }

            if (!saveWifiSettings(candidate)) {
                JsonDocument error;
                error["error"] = "storage_failed";
                error["message"] = "Could not store Wi-Fi settings";
                if (requestId > 0) error["request_id"] = requestId;
                sendJson(request, 500, error);
                return;
            }

            JsonDocument accepted;
            accepted["accepted"] = true;
            accepted["restarting"] = true;
            accepted["hostname"] = candidate.hostname;
            if (requestId > 0) accepted["request_id"] = requestId;
            sendJson(request, 202, accepted);
            restartAtMs = millis() + 1000;
        }
    );
    wifiHandler->setMethod(HTTP_POST);
    wifiHandler->setMaxContentLength(512);
    server.addHandler(wifiHandler);

    server.on("/api/wifi", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!wifiSetupMode) {
            request->send(403, "application/json", "{\"error\":\"setup_required\"}");
            return;
        }
        if (!clearWifiSettings()) {
            request->send(500, "application/json", "{\"error\":\"storage_failed\"}");
            return;
        }
        request->send(202, "application/json", "{\"accepted\":true,\"restarting\":true}");
        restartAtMs = millis() + 1000;
    });

    Bridge.begin(&server);
    server.begin();
    webServerStarted = true;
    Serial.printf("[HTTP] Server listening on port %u\n", HTTP_PORT);
}

void maintainWiFi() {
    const bool connected = WiFi.status() == WL_CONNECTED;

    if (connected && !wifiWasConnected) {
        wifiWasConnected = true;
        startNetworkServices();
        return;
    }

    if (!connected && wifiWasConnected) {
        wifiWasConnected = false;
        if (mdnsStarted) {
            MDNS.end();
            mdnsStarted = false;
        }
        StatusLed.setState(LedState::WIFI_CONNECTING);
        Serial.println("[WIFI] Station disconnected; waiting to reconnect.");
    }

    // Arduino's station layer owns reconnection when setAutoReconnect(true) is
    // enabled. Calling WiFi.reconnect() while an association or DHCP exchange is
    // still in progress forces ASSOC_LEAVE and also makes scans fail with
    // ESP_ERR_WIFI_STATE.
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=================================");
    Serial.println("   TONEX Wireless Bridge v1.2   ");
    Serial.println("=================================");

    StatusLed.begin();
    WiFi.onEvent(logWiFiEvent);
    ToneX.begin();
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED && ToneX.isConnected()) {
        StatusLed.setState(LedState::TONEX_CONNECTED);
    }
}

void loop() {
    maintainWiFi();
    ToneX.loop();
    StatusLed.update();
    if (restartAtMs > 0 && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
        ESP.restart();
    }
    delay(2);
}

#endif
