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
            broadcastStatus(ToneX.isConnected(), ToneX.activePreset());
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
        broadcastStatus(connected, ToneX.activePreset());
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

    ToneX.onActivePresetChange([this](uint8_t presetIndex) {
        broadcastStatus(ToneX.isConnected(), presetIndex);
    });
}
#endif

void WsBridge::broadcastStatus(bool tonexConnected, int16_t activePc) {
    JsonDocument doc;
    doc["event"] = "status";
    doc["tonex_connected"] = tonexConnected;
    if (activePc >= 0 && activePc < TONEX_TOTAL_PRESETS) {
        doc["active_pc"] = activePc;
        ToneXHDLC::BankSlot bs = ToneXHDLC::bankSlotFromPC(static_cast<uint8_t>(activePc));
        doc["active_bank"] = bs.bank;
        doc["active_slot"] = std::string(1, bs.slot);
    }

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

void WsBridge::broadcastMidiAccepted(uint8_t pc, uint32_t requestId) {
    JsonDocument doc;
    doc["event"] = "midi_accepted";
    doc["pc"] = pc;
    if (requestId > 0) doc["request_id"] = requestId;

    std::string out;
    serializeJson(doc, out);

#ifndef NATIVE_TEST
    _ws.textAll(out.c_str());
#endif
}

void WsBridge::broadcastError(const char* code, const char* message, uint32_t requestId) {
    JsonDocument doc;
    doc["event"] = "error";
    doc["code"] = code;
    doc["message"] = message;
    if (requestId > 0) doc["request_id"] = requestId;

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
        int bank = doc["bank"] | -1;
        const char* slotStr = doc["slot"] | "A";
        char slot = slotStr[0];
        int channel = doc["channel"] | -1;
        uint32_t requestId = doc["request_id"] | 0;

        if (bank < 0 || bank >= 50 || (slot != 'A' && slot != 'B' && slot != 'C') ||
            channel < 0 || channel >= 16) {
            broadcastError("midi_invalid", "Invalid preset or MIDI channel", requestId);
            return;
        }

        if (!ToneX.sendBankSelectAndPC(
                static_cast<uint8_t>(bank), slot, static_cast<uint8_t>(channel))) {
            broadcastError(
                "midi_unavailable", "The TONEX MIDI interface is not ready", requestId);
            return;
        }
        uint8_t pc = ToneXHDLC::pcFromBankSlot(static_cast<uint8_t>(bank), slot);
        broadcastMidiAccepted(pc, requestId);

    } else if (strcmp(action, "status_request") == 0) {
        broadcastStatus(ToneX.isConnected(), ToneX.activePreset());

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
