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
            // Initial status is connection-local and includes the identity used for sync ownership.
            sendStatus(client->id(), ToneX.isConnected(), ToneX.activePreset());
        } else if (type == WS_EVT_DISCONNECT) {
            if (_syncOwnerClientId == client->id()) {
                const uint32_t abandonedOwner = _syncOwnerClientId;
                _syncOwnerClientId = 0;
                if (ToneX.isSyncing()) ToneX.cancelSync();
                StatusLed.setState(ToneX.isConnected() ? LedState::TONEX_CONNECTED : LedState::WIFI_CONNECTED);

                JsonDocument doc;
                doc["event"] = "sync_cancelled";
                doc["reason"] = "owner_disconnected";
                doc["owner_client_id"] = abandonedOwner;
                std::string out;
                serializeJson(doc, out);
                _ws.textAll(out.c_str());
            }
        } else if (type == WS_EVT_DATA) {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                std::string msg((char*)data, len);
                processIncomingMessage(msg, client->id());
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
        _syncOwnerClientId = 0;
    });

    ToneX.onSyncError([this](const std::string& message) {
        StatusLed.setState(LedState::ERROR_STATE);
        broadcastError("sync_failed", message.c_str());
        _syncOwnerClientId = 0;
    });

    ToneX.onPresetReceived([this](const ToneXPresetInfo& info) {
        broadcastPreset(info.bank, info.slot, info.name, info.amp, info.cab);
    });

    ToneX.onActivePresetChange([this](uint8_t presetIndex) {
        broadcastStatus(ToneX.isConnected(), presetIndex);
    });
}

void WsBridge::sendStatus(uint32_t clientId, bool tonexConnected, int16_t activePc) {
    JsonDocument doc;
    doc["event"] = "status";
    doc["client_id"] = clientId;
    doc["tonex_connected"] = tonexConnected;
    if (activePc >= 0 && activePc < TONEX_TOTAL_PRESETS) {
        doc["active_pc"] = activePc;
        ToneXHDLC::BankSlot bs = ToneXHDLC::bankSlotFromPC(static_cast<uint8_t>(activePc));
        doc["active_bank"] = bs.bank;
        doc["active_slot"] = std::string(1, bs.slot);
    }

    std::string out;
    serializeJson(doc, out);
    _ws.text(clientId, out.c_str());
}

void WsBridge::sendMidiAccepted(uint32_t clientId, uint8_t pc, uint32_t requestId) {
    JsonDocument doc;
    doc["event"] = "midi_accepted";
    doc["pc"] = pc;
    if (requestId > 0) doc["request_id"] = requestId;

    std::string out;
    serializeJson(doc, out);
    _ws.text(clientId, out.c_str());
}

void WsBridge::sendMidiControlAccepted(uint32_t clientId, uint8_t control, uint8_t value, uint32_t requestId) {
    JsonDocument doc;
    doc["event"] = "midi_cc_accepted";
    doc["cc"] = control;
    doc["value"] = value;
    if (requestId > 0) doc["request_id"] = requestId;

    std::string out;
    serializeJson(doc, out);
    _ws.text(clientId, out.c_str());
}

void WsBridge::sendError(uint32_t clientId, const char* code, const char* message, uint32_t requestId) {
    JsonDocument doc;
    doc["event"] = "error";
    doc["code"] = code;
    doc["message"] = message;
    if (requestId > 0) doc["request_id"] = requestId;

    std::string out;
    serializeJson(doc, out);
    _ws.text(clientId, out.c_str());
}

void WsBridge::sendSyncStarted(uint32_t clientId, uint32_t requestId) {
    JsonDocument doc;
    doc["event"] = "sync_started";
    doc["owner_client_id"] = clientId;
    doc["total"] = TONEX_TOTAL_PRESETS;
    if (requestId > 0) doc["request_id"] = requestId;

    std::string out;
    serializeJson(doc, out);
    _ws.text(clientId, out.c_str());
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
    if (_syncOwnerClientId > 0) doc["owner_client_id"] = _syncOwnerClientId;

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
    if (_syncOwnerClientId > 0) doc["owner_client_id"] = _syncOwnerClientId;

    std::string out;
    serializeJson(doc, out);

#ifndef NATIVE_TEST
    _ws.textAll(out.c_str());
#endif
}

void WsBridge::broadcastSyncCancelled() {
    JsonDocument doc;
    doc["event"] = "sync_cancelled";
    if (_syncOwnerClientId > 0) doc["owner_client_id"] = _syncOwnerClientId;

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

void WsBridge::processIncomingMessage(const std::string& jsonString, uint32_t clientId) {
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
#ifndef NATIVE_TEST
            sendError(clientId, "midi_invalid", "Invalid preset or MIDI channel", requestId);
#endif
            return;
        }

        if (!ToneX.sendBankSelectAndPC(
                static_cast<uint8_t>(bank), slot, static_cast<uint8_t>(channel))) {
#ifndef NATIVE_TEST
            sendError(clientId, "midi_unavailable", "The TONEX MIDI interface is not ready", requestId);
#endif
            return;
        }
        uint8_t pc = ToneXHDLC::pcFromBankSlot(static_cast<uint8_t>(bank), slot);
#ifndef NATIVE_TEST
        sendMidiAccepted(clientId, pc, requestId);
#endif

    } else if (strcmp(action, "midi_cc") == 0) {
        const int control = doc["cc"] | -1;
        const int value = doc["value"] | -1;
        const int channel = doc["channel"] | -1;
        const uint32_t requestId = doc["request_id"] | 0;
        const bool supportedControl = control == 14 || control == 18 || control == 75 ||
            control == 117 || control == 122;

        if (!supportedControl || value < 0 || value > 127 || channel < 0 || channel >= 16) {
#ifndef NATIVE_TEST
            sendError(clientId, "midi_invalid", "Unsupported MIDI control, value, or channel", requestId);
#endif
            return;
        }

        if (!ToneX.sendControlChange(
                static_cast<uint8_t>(control), static_cast<uint8_t>(value), static_cast<uint8_t>(channel))) {
#ifndef NATIVE_TEST
            sendError(clientId, "midi_unavailable", "The TONEX MIDI interface is not ready", requestId);
#endif
            return;
        }
#ifndef NATIVE_TEST
        sendMidiControlAccepted(clientId, static_cast<uint8_t>(control), static_cast<uint8_t>(value), requestId);
#endif

    } else if (strcmp(action, "status_request") == 0) {
#ifndef NATIVE_TEST
        sendStatus(clientId, ToneX.isConnected(), ToneX.activePreset());
#endif

    } else if (strcmp(action, "sync_start") == 0) {
        uint32_t requestId = doc["request_id"] | 0;
        if (_syncOwnerClientId != 0 || ToneX.isSyncing()) {
#ifndef NATIVE_TEST
            sendError(clientId, "sync_unavailable", "Preset sync is already active or the TONEX is disconnected", requestId);
#endif
        } else {
            _syncOwnerClientId = clientId;
            if (ToneX.startSync()) {
                StatusLed.setState(LedState::SYNCING);
#ifndef NATIVE_TEST
                sendSyncStarted(clientId, requestId);
#endif
            } else {
                _syncOwnerClientId = 0;
#ifndef NATIVE_TEST
                sendError(clientId, "sync_unavailable", "Preset sync is already active or the TONEX is disconnected", requestId);
#endif
            }
        }
    } else if (strcmp(action, "sync_cancel") == 0) {
        uint32_t requestId = doc["request_id"] | 0;
        if (ToneX.isSyncing() && _syncOwnerClientId != clientId) {
#ifndef NATIVE_TEST
            sendError(clientId, "sync_not_owner", "Only the browser that started sync can cancel it", requestId);
#endif
        } else if (ToneX.isSyncing()) {
            ToneX.cancelSync();
            StatusLed.setState(LedState::TONEX_CONNECTED);
            broadcastSyncCancelled();
            _syncOwnerClientId = 0;
        } else {
#ifndef NATIVE_TEST
            sendError(clientId, "sync_not_active", "No preset sync is currently active", requestId);
#endif
        }
    } else {
#ifndef NATIVE_TEST
        sendError(clientId, "unknown_action", "Unknown bridge action");
#endif
    }
}
