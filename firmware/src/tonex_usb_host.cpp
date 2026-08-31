#include "tonex_usb_host.h"
#include "tonex_hdlc.h"
#include "config.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#else
#include <chrono>
#include <thread>
static uint32_t millis() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
static void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
#endif

ToneXUsbHost ToneX;

ToneXUsbHost::ToneXUsbHost()
    : _connected(false), _syncing(false), _syncIndex(0), _lastSyncStepMs(0) {}

ToneXUsbHost::~ToneXUsbHost() {}

bool ToneXUsbHost::begin() {
    // In production firmware on ESP32-S3, initializes USB Host CDC & MIDI descriptors
    _connected = true;
    if (_connCb) _connCb(_connected);
    return true;
}

void ToneXUsbHost::loop() {
    if (!_syncing) return;

    // Non-blocking sync state machine step
    uint32_t now = millis();
    if (now - _lastSyncStepMs < 40) return; // 40ms interval between preset queries
    _lastSyncStepMs = now;

    if (_syncIndex < TONEX_TOTAL_PRESETS) {
        ToneXHDLC::BankSlot bs = ToneXHDLC::bankSlotFromPC(_syncIndex);
        std::vector<uint8_t> req = ToneXHDLC::createPresetRequest(_syncIndex);
        std::vector<uint8_t> frame = ToneXHDLC::buildFrame(req.data(), req.size());
        sendCdcFrame(frame);

        // Notify progress
        if (_progCb) _progCb(_syncIndex + 1, TONEX_TOTAL_PRESETS);

        // Process response (in real hardware this reads from CDC endpoint)
        ToneXPresetInfo info;
        info.bank = bs.bank;
        info.slot = bs.slot;
        info.name = "Preset " + std::to_string(bs.bank) + bs.slot;
        info.amp = true;
        info.cab = true;

        if (_presetCb) _presetCb(info);

        _syncIndex++;
        if (_syncIndex >= TONEX_TOTAL_PRESETS) {
            _syncing = false;
            _syncIndex = 0;
            if (_completeCb) _completeCb(TONEX_TOTAL_PRESETS);
        }
    }
}

bool ToneXUsbHost::isConnected() const {
    return _connected;
}

bool ToneXUsbHost::sendBankSelectAndPC(uint8_t bank, char slot, uint8_t channel) {
    ToneXHDLC::MidiMessage msg = ToneXHDLC::getMidiBankSelectAndPC(bank, slot, channel);
    // In hardware, pushes raw MIDI USB packets to MIDI out endpoint
    return true;
}

bool ToneXUsbHost::startSync() {
    if (_syncing || !_connected) return false;
    _syncing = true;
    _syncIndex = 0;
    _lastSyncStepMs = millis();

    // Send Hello and Request State commands
    std::vector<uint8_t> hello = ToneXHDLC::buildFrame(ToneXHDLC::HELLO_CMD, sizeof(ToneXHDLC::HELLO_CMD));
    sendCdcFrame(hello);
    return true;
}

void ToneXUsbHost::cancelSync() {
    _syncing = false;
    _syncIndex = 0;
}

bool ToneXUsbHost::isSyncing() const {
    return _syncing;
}

void ToneXUsbHost::onConnectionChange(ConnectionCallback cb) {
    _connCb = cb;
}

void ToneXUsbHost::onSyncProgress(SyncProgressCallback cb) {
    _progCb = cb;
}

void ToneXUsbHost::onSyncComplete(SyncCompleteCallback cb) {
    _completeCb = cb;
}

void ToneXUsbHost::onPresetReceived(PresetReceivedCallback cb) {
    _presetCb = cb;
}

bool ToneXUsbHost::sendCdcFrame(const std::vector<uint8_t>& frame) {
    // In production firmware, writes to CDC bulk OUT endpoint
    return true;
}

std::vector<uint8_t> ToneXUsbHost::readCdcFrame(uint32_t timeoutMs) {
    // In production firmware, reads from CDC bulk IN endpoint
    return {};
}
