#ifndef NATIVE_TEST

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "tonex_usb_host.h"
#include "ws_bridge.h"
#include "led_status.h"

AsyncWebServer server(HTTP_PORT);
static bool webServerStarted = false;
static bool mdnsStarted = false;
static bool wifiWasConnected = false;
static uint32_t lastWifiReconnectMs = 0;

void setupWebServer();

static void startNetworkServices() {
    const IPAddress address = WiFi.localIP();
    Serial.print("[WIFI] Connected. IP address: ");
    Serial.println(address);

    if (!mdnsStarted && MDNS.begin(TONEX_MDNS_NAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        mdnsStarted = true;
        Serial.println("[MDNS] Responder started: http://" TONEX_MDNS_NAME ".local");
    }

    if (!webServerStarted) {
        setupWebServer();
    }

    StatusLed.setState(ToneX.isConnected() ? LedState::TONEX_CONNECTED : LedState::WIFI_CONNECTED);
}

bool setupWiFi() {
    StatusLed.setState(LedState::WIFI_CONNECTING);

    if (TONEX_WIFI_SSID[0] == '\0') {
        Serial.println("[WIFI] No station credentials configured.");
        Serial.println("[WIFI] Copy include/wifi_secrets.example.h to include/wifi_secrets.h and configure the WLAN.");
        return false;
    }

    Serial.println("[WIFI] Connecting to station network: " TONEX_WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(TONEX_MDNS_NAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(TONEX_WIFI_SSID, TONEX_WIFI_PASS);

    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < TONEX_WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    wifiWasConnected = WiFi.status() == WL_CONNECTED;
    if (!wifiWasConnected) {
        Serial.println("[WIFI] Initial connection timed out; reconnecting in the background.");
        return false;
    }

    startNetworkServices();
    return true;
}

void setupWebServer() {
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] Error mounting LittleFS");
    } else {
        Serial.println("[FS] LittleFS mounted successfully");
    }

    // Let the shared web application positively identify an ESP32 bridge host.
    server.on("/api/bridge", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", "{\"service\":\"tonex-bridge\",\"protocol_version\":1}");
    });

    // Serve web application from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Initialize WebSocket Bridge
    Bridge.begin(&server);

    server.begin();
    webServerStarted = true;
    Serial.println("[HTTP] Server listening on port " + String(HTTP_PORT));
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

    if (!connected && TONEX_WIFI_SSID[0] != '\0') {
        const uint32_t now = millis();
        if (now - lastWifiReconnectMs >= TONEX_WIFI_RECONNECT_INTERVAL_MS) {
            lastWifiReconnectMs = now;
            Serial.println("[WIFI] Reconnect requested.");
            WiFi.reconnect();
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=================================");
    Serial.println("   TONEX Wireless Bridge v1.2   ");
    Serial.println("=================================");

    StatusLed.begin();
    setupWiFi();

    ToneX.begin();
    if (WiFi.status() == WL_CONNECTED && ToneX.isConnected()) {
        StatusLed.setState(LedState::TONEX_CONNECTED);
    }
}

void loop() {
    maintainWiFi();
    ToneX.loop();
    StatusLed.update();
    delay(2);
}

#endif
