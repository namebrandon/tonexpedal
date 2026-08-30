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

void setupWiFi() {
    StatusLed.setState(LedState::WIFI_CONNECTING);
    Serial.println("[WIFI] Starting AP: " TONEX_AP_SSID);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(TONEX_AP_SSID, TONEX_AP_PASS);

    Serial.print("[WIFI] AP IP address: ");
    Serial.println(WiFi.softAPIP());

    if (MDNS.begin(TONEX_MDNS_NAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.println("[MDNS] Responder started: http://" TONEX_MDNS_NAME ".local");
    }

    StatusLed.setState(LedState::WIFI_CONNECTED);
}

void setupWebServer() {
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] Error mounting LittleFS");
    } else {
        Serial.println("[FS] LittleFS mounted successfully");
    }

    // Serve web application from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Initialize WebSocket Bridge
    Bridge.begin(&server);

    server.begin();
    Serial.println("[HTTP] Server listening on port " + String(HTTP_PORT));
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=================================");
    Serial.println("   TONEX Wireless Bridge v1.2   ");
    Serial.println("=================================");

    StatusLed.begin();
    setupWiFi();
    setupWebServer();

    ToneX.begin();
    if (ToneX.isConnected()) {
        StatusLed.setState(LedState::TONEX_CONNECTED);
    }
}

void loop() {
    ToneX.loop();
    StatusLed.update();
    delay(2);
}

#endif
