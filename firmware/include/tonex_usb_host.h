#pragma once

#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <vector>
#include <string>

struct ToneXPresetInfo {
    uint8_t bank;
    char slot;
    std::string name;
    bool amp;
    bool cab;
};

class ToneXUsbHost {
public:
    typedef std::function<void(bool connected)> ConnectionCallback;
    typedef std::function<void(uint8_t loaded, uint8_t total)> SyncProgressCallback;
    typedef std::function<void(const ToneXPresetInfo& info)> PresetReceivedCallback;

    ToneXUsbHost();
    ~ToneXUsbHost();

    bool begin();
    void loop();

    bool isConnected() const;

    // MIDI
    bool sendBankSelectAndPC(uint8_t bank, char slot, uint8_t channel = 0);

    // USB Sync (HDLC Presets Dump)
    bool startSync();
    void cancelSync();
    bool isSyncing() const;

    // Callbacks
    void onConnectionChange(ConnectionCallback cb);
    void onSyncProgress(SyncProgressCallback cb);
    void onPresetReceived(PresetReceivedCallback cb);

private:
    bool _connected;
    bool _syncing;
    uint8_t _syncIndex;
    uint32_t _lastSyncStepMs;

    ConnectionCallback _connCb;
    SyncProgressCallback _progCb;
    PresetReceivedCallback _presetCb;

    bool sendCdcFrame(const std::vector<uint8_t>& frame);
    std::vector<uint8_t> readCdcFrame(uint32_t timeoutMs);
};

extern ToneXUsbHost ToneX;
