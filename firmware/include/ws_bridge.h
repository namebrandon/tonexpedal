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
    void broadcastError(const char* code, const char* message, uint32_t requestId = 0);
    void broadcastPreset(uint8_t bank, char slot, const std::string& name, bool amp, bool cab);

    void processIncomingMessage(const std::string& jsonString, uint32_t clientId = 0);

private:
#ifndef NATIVE_TEST
    AsyncWebSocket _ws;
    void sendStatus(uint32_t clientId, bool tonexConnected, int16_t activePc = -1);
    void sendMidiAccepted(uint32_t clientId, uint8_t pc, uint32_t requestId = 0);
    void sendMidiControlAccepted(uint32_t clientId, uint8_t control, uint8_t value, uint32_t requestId = 0);
    void sendError(uint32_t clientId, const char* code, const char* message, uint32_t requestId = 0);
    void sendSyncStarted(uint32_t clientId, uint32_t requestId = 0);
#endif
    uint32_t _syncOwnerClientId = 0;
};

extern WsBridge Bridge;
