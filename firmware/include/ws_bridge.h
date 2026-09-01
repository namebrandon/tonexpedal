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

    void broadcastStatus(bool tonexConnected, int16_t activePc = -1);
    void broadcastSyncProgress(uint8_t loaded, uint8_t total);
    void broadcastSyncComplete(uint8_t total);
    void broadcastSyncCancelled();
    void broadcastMidiAccepted(uint8_t pc, uint32_t requestId = 0);
    void broadcastError(const char* code, const char* message, uint32_t requestId = 0);
    void broadcastPreset(uint8_t bank, char slot, const std::string& name, bool amp, bool cab);

    void processIncomingMessage(const std::string& jsonString);

private:
#ifndef NATIVE_TEST
    AsyncWebSocket _ws;
#endif
};

extern WsBridge Bridge;
