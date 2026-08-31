#pragma once

#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <vector>
#include <string>

#ifndef NATIVE_TEST
#include <usb/usb_host.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

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
    typedef std::function<void(uint8_t total)> SyncCompleteCallback;
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
    void onSyncComplete(SyncCompleteCallback cb);
    void onPresetReceived(PresetReceivedCallback cb);

private:
    volatile bool _connected;
    bool _syncing;
    uint8_t _syncIndex;
    uint32_t _lastSyncStepMs;

#ifndef NATIVE_TEST
    bool _hostInstalled;
    usb_host_client_handle_t _clientHandle;
    usb_device_handle_t _deviceHandle;
    TaskHandle_t _libraryTaskHandle;
    uint8_t _midiInterfaceNumber;
    uint8_t _midiAlternateSetting;
    uint8_t _midiEndpointOut;

    static void libraryTask(void* arg);
    static void clientEventCallback(const usb_host_client_event_msg_t* event, void* arg);
    void handleNewDevice(uint8_t address);
    void handleDeviceGone(usb_device_handle_t device);
    void logConfiguration(const usb_config_desc_t* config) const;
    bool claimMidiInterface(const usb_config_desc_t* config);
    bool submitMidiPacket(const uint8_t packet[4]);
    static void midiTransferCallback(usb_transfer_t* transfer);
#endif

    ConnectionCallback _connCb;
    SyncProgressCallback _progCb;
    SyncCompleteCallback _completeCb;
    PresetReceivedCallback _presetCb;

    bool sendCdcFrame(const std::vector<uint8_t>& frame);
    std::vector<uint8_t> readCdcFrame(uint32_t timeoutMs);
};

extern ToneXUsbHost ToneX;
