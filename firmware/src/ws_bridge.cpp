#include "ws_bridge.h"
#include "tonex_usb_host.h"
#include "tonex_hdlc.h"
#include "config.h"
#include "led_status.h"
#include <ArduinoJson.h>

WsBridge Bridge;

WsBridge::WsBridge()
#ifndef NATIVE_TEST
    : _ws(WS_PATH)
#endif
{}

WsBridge::~WsBridge() {}

#ifndef NATIVE_TEST
void WsBridge::begin(AsyncWebServer* server) {
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            // Send initial status
            broadcastStatus(ToneX.isConnected());
        } else if (type == WS_EVT_DATA) {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                std::string msg((char*)data, len);
                processIncomingMessage(msg);
            }
        }
    });

    server->addHandler(&_ws);

    // Link ToneX callbacks to WebSocket broadcasts
    ToneX.onConnectionChange([this](bool connected) {
        StatusLed.setState(connected ? LedState::TONEX_CONNECTED : LedState::WIFI_CONNECTED);
        broadcastStatus(connected);
    });

    ToneX.onSyncProgress([this](uint8_t loaded, uint8_t total) {
        broadcastSyncProgress(loaded, total);
    });

    ToneX.onSyncComplete([this](uint8_t total) {
        StatusLed.setState(LedState::TONEX_CONNECTED);
        broadcastSyncComplete(total);
    });

    ToneX.onSyncError([this](const std::string& message) {
        StatusLed.setState(LedState::ERROR_STATE);
        broadcastError("sync_failed", message.c_str());
    });

    ToneX.onPresetReceived([this](const ToneXPresetInfo& info) {
        broadcastPreset(info.bank, info.slot, info.name, info.amp, info.cab);
    });
}
#endif

void WsBridge::broadcastStatus(bool tonexConnected, uint8_t activePc) {
    JsonDocument doc;
    doc["event"] = "status";
    doc["tonex_connected"] = tonexConnected;
    doc["active_pc"] = activePc;
    ToneXHDLC::BankSlot bs = ToneXHDLC::bankSlotFromPC(activePc);
    doc["active_bank"] = bs.bank;
    doc["active_slot"] = std::string(1, bs.slot);

    std::string out;
    serializeJson(doc, out);

#ifndef NATIVE_TEST
    _ws.textAll(out.c_str());
#endif
}

void WsBridge::broadcastSyncProgress(uint8_t loaded, uint8_t total) {
    JsonDocument doc;
    doc["event"] = "sync_progress";
    doc["loaded"] = loaded;
    doc["total"] = total;
    doc["percent"] = (loaded * 100) / total;

    std::string out;
    serializeJson(doc, out);

#ifndef NATIVE_TEST
    _ws.textAll(out.c_str());
#endif
}

void WsBridge::broadcastSyncComplete(uint8_t total) {
    JsonDocument doc;
    doc["event"] = "sync_complete";
    doc["total"] = total;

    std::string out;
    serializeJson(doc, out);

#ifndef NATIVE_TEST
    _ws.textAll(out.c_str());
#endif
}

void WsBridge::broadcastSyncCancelled() {
    JsonDocument doc;
    doc["event"] = "sync_cancelled";

    std::string out;
    serializeJson(doc, out);

#ifndef NATIVE_TEST
    _ws.textAll(out.c_str());
#endif
}

void WsBridge::broadcastError(const char* code, const char* message) {
    JsonDocument doc;
    doc["event"] = "error";
    doc["code"] = code;
    doc["message"] = message;

    std::string out;
    serializeJson(doc, out);

#ifndef NATIVE_TEST
    _ws.textAll(out.c_str());
#endif
}

void WsBridge::broadcastPreset(uint8_t bank, char slot, const std::string& name, bool amp, bool cab) {
    JsonDocument doc;
    doc["event"] = "preset_update";
    doc["bank"] = bank;
    doc["slot"] = std::string(1, slot);
    doc["name"] = name;
    doc["amp"] = amp;
    doc["cab"] = cab;

    std::string out;
    serializeJson(doc, out);

#ifndef NATIVE_TEST
    _ws.textAll(out.c_str());
#endif
}

void WsBridge::processIncomingMessage(const std::string& jsonString) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonString);
    if (err) return;

    const char* action = doc["action"] | "";

    if (strcmp(action, "midi_send") == 0) {
        uint8_t bank = doc["bank"] | 0;
        const char* slotStr = doc["slot"] | "A";
        char slot = slotStr[0];
        uint8_t channel = doc["channel"] | 0;

        if (!ToneX.sendBankSelectAndPC(bank, slot, channel)) {
            broadcastError("midi_unavailable", "The TONEX MIDI interface is not ready");
            return;
        }
        uint8_t pc = ToneXHDLC::pcFromBankSlot(bank, slot);
        broadcastStatus(ToneX.isConnected(), pc);

    } else if (strcmp(action, "sync_start") == 0) {
        if (ToneX.startSync()) {
            StatusLed.setState(LedState::SYNCING);
        } else {
            broadcastError("sync_unavailable", "Preset sync is already active or the TONEX is disconnected");
        }
    } else if (strcmp(action, "sync_cancel") == 0) {
        if (ToneX.isSyncing()) {
            ToneX.cancelSync();
            StatusLed.setState(LedState::TONEX_CONNECTED);
            broadcastSyncCancelled();
        } else {
            broadcastError("sync_not_active", "No preset sync is currently active");
        }
    } else {
        broadcastError("unknown_action", "Unknown bridge action");
    }
}
