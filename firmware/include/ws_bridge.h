#pragma once

#include <stdint.h>
#include <string>

#ifndef NATIVE_TEST
#include <ESPAsyncWebServer.h>
#endif

class WsBridge {
public:
    WsBridge();
    ~WsBridge();

#ifndef NATIVE_TEST
    void begin(AsyncWebServer* server);
#endif

    void broadcastStatus(bool tonexConnected, uint8_t activePc = 0);
    void broadcastSyncProgress(uint8_t loaded, uint8_t total);
    void broadcastSyncComplete(uint8_t total);
    void broadcastSyncCancelled();
    void broadcastError(const char* code, const char* message);
    void broadcastPreset(uint8_t bank, char slot, const std::string& name, bool amp, bool cab);

    void processIncomingMessage(const std::string& jsonString);

private:
#ifndef NATIVE_TEST
    AsyncWebSocket _ws;
#endif
};

extern WsBridge Bridge;
