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
#include <freertos/stream_buffer.h>
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
    typedef std::function<void(const std::string& message)> SyncErrorCallback;
    typedef std::function<void(const ToneXPresetInfo& info)> PresetReceivedCallback;
    typedef std::function<void(uint8_t presetIndex)> ActivePresetCallback;

    ToneXUsbHost();
    ~ToneXUsbHost();

    bool begin();
    void loop();

    bool isConnected() const;
    int16_t activePreset() const;

    // MIDI
    bool sendBankSelectAndPC(uint8_t bank, char slot, uint8_t channel = 0);
    bool sendControlChange(uint8_t control, uint8_t value, uint8_t channel = 0);

    // USB Sync (HDLC Presets Dump)
    bool startSync();
    void cancelSync();
    bool isSyncing() const;

    // Callbacks
    void onConnectionChange(ConnectionCallback cb);
    void onSyncProgress(SyncProgressCallback cb);
    void onSyncComplete(SyncCompleteCallback cb);
    void onSyncError(SyncErrorCallback cb);
    void onPresetReceived(PresetReceivedCallback cb);
    void onActivePresetChange(ActivePresetCallback cb);

private:
    volatile bool _connected;
    volatile bool _syncing;
    uint8_t _syncIndex;
    uint32_t _lastSyncStepMs;
    int16_t _activePreset;

#ifndef NATIVE_TEST
    bool _hostInstalled;
    usb_host_client_handle_t _clientHandle;
    usb_device_handle_t _deviceHandle;
    usb_device_handle_t _goneDeviceHandle;
    TaskHandle_t _libraryTaskHandle;
    uint8_t _midiInterfaceNumber;
    uint8_t _midiAlternateSetting;
    uint8_t _midiEndpointOut;
    uint8_t _cdcControlInterfaceNumber;
    uint8_t _cdcControlAlternateSetting;
    uint8_t _cdcDataInterfaceNumber;
    uint8_t _cdcDataAlternateSetting;
    uint8_t _cdcEndpointIn;
    uint8_t _cdcEndpointOut;
    uint16_t _cdcEndpointInMaxPacket;
    volatile bool _cdcReady;
    StreamBufferHandle_t _cdcRxStream;
    usb_transfer_t* _cdcInTransfer;
    TaskHandle_t _syncTaskHandle;
    volatile uint16_t _midiTransfersInFlight;
    volatile uint16_t _cdcOutTransfersInFlight;
    std::vector<uint8_t> _cdcFrameBuffer;
    bool _cdcInsideFrame;

    static void libraryTask(void* arg);
    static void clientEventCallback(const usb_host_client_event_msg_t* event, void* arg);
    void handleNewDevice(uint8_t address);
    void handleDeviceGone(usb_device_handle_t device);
    void logConfiguration(const usb_config_desc_t* config) const;
    bool claimMidiInterface(const usb_config_desc_t* config);
    bool submitMidiPacket(const uint8_t packet[4]);
    static void midiTransferCallback(usb_transfer_t* transfer);
    bool claimCdcInterfaces(const usb_config_desc_t* config);
    bool submitCdcControlRequest(uint8_t request);
    bool submitCdcRead();
    static void cdcControlTransferCallback(usb_transfer_t* transfer);
    static void cdcInTransferCallback(usb_transfer_t* transfer);
    static void cdcOutTransferCallback(usb_transfer_t* transfer);
    void resetClaimedInterfaces();
    void cleanupGoneDevice();
    static void syncTask(void* arg);
    void runSync();
    void failSync(const std::string& message);
    void handleCdcEvent(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> readCdcResponse(uint32_t timeoutMs, int expectedPresetIndex = -1);
#endif

    ConnectionCallback _connCb;
    SyncProgressCallback _progCb;
    SyncCompleteCallback _completeCb;
    SyncErrorCallback _syncErrorCb;
    PresetReceivedCallback _presetCb;
    ActivePresetCallback _activePresetCb;

    bool sendCdcFrame(const std::vector<uint8_t>& frame);
    std::vector<uint8_t> readCdcFrame(uint32_t timeoutMs, bool syncOnly = true);
};

extern ToneXUsbHost ToneX;
