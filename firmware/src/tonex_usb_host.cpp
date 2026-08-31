#include "tonex_usb_host.h"
#include "tonex_hdlc.h"
#include "config.h"
#include <cstring>

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <esp_err.h>
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
    : _connected(false), _syncing(false), _syncIndex(0), _lastSyncStepMs(0)
#ifndef NATIVE_TEST
    , _hostInstalled(false), _clientHandle(nullptr), _deviceHandle(nullptr), _libraryTaskHandle(nullptr),
      _midiInterfaceNumber(0xFF), _midiAlternateSetting(0), _midiEndpointOut(0)
#endif
{}

ToneXUsbHost::~ToneXUsbHost() {}

bool ToneXUsbHost::begin() {
#ifdef NATIVE_TEST
    return false;
#else
    usb_host_config_t hostConfig = {};
    hostConfig.skip_phy_setup = false;
    hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;

    esp_err_t err = usb_host_install(&hostConfig);
    if (err != ESP_OK) {
        Serial.printf("[USB] Host installation failed: %s\n", esp_err_to_name(err));
        return false;
    }
    _hostInstalled = true;

    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        libraryTask,
        "tonex-usb-lib",
        4096,
        this,
        2,
        &_libraryTaskHandle,
        0
    );
    if (taskCreated != pdPASS) {
        Serial.println("[USB] Could not start the host library event task");
        usb_host_uninstall();
        _hostInstalled = false;
        return false;
    }

    usb_host_client_config_t clientConfig = {};
    clientConfig.is_synchronous = false;
    clientConfig.max_num_event_msg = 8;
    clientConfig.async.client_event_callback = clientEventCallback;
    clientConfig.async.callback_arg = this;

    err = usb_host_client_register(&clientConfig, &_clientHandle);
    if (err != ESP_OK) {
        Serial.printf("[USB] Client registration failed: %s\n", esp_err_to_name(err));
        vTaskDelete(_libraryTaskHandle);
        _libraryTaskHandle = nullptr;
        usb_host_uninstall();
        _hostInstalled = false;
        return false;
    }

    Serial.println("[USB] Host ready; waiting for a TONEX device (VID 0x1963)");
    return true;
#endif
}

void ToneXUsbHost::loop() {
#ifndef NATIVE_TEST
    if (_clientHandle) {
        const esp_err_t err = usb_host_client_handle_events(_clientHandle, 0);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            Serial.printf("[USB] Client event error: %s\n", esp_err_to_name(err));
        }
    }
#endif

}

#ifndef NATIVE_TEST
void ToneXUsbHost::libraryTask(void* arg) {
    ToneXUsbHost* host = static_cast<ToneXUsbHost*>(arg);
    while (host->_hostInstalled) {
        uint32_t eventFlags = 0;
        const esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
        if (err != ESP_OK) {
            Serial.printf("[USB] Library event error: %s\n", esp_err_to_name(err));
        }
    }
    vTaskDelete(nullptr);
}

void ToneXUsbHost::clientEventCallback(const usb_host_client_event_msg_t* event, void* arg) {
    ToneXUsbHost* host = static_cast<ToneXUsbHost*>(arg);
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        host->handleNewDevice(event->new_dev.address);
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        host->handleDeviceGone(event->dev_gone.dev_hdl);
    }
}

void ToneXUsbHost::handleNewDevice(uint8_t address) {
    if (_deviceHandle) {
        Serial.printf("[USB] Ignoring device at address %u while the TONEX is open\n", address);
        return;
    }

    usb_device_handle_t device = nullptr;
    esp_err_t err = usb_host_device_open(_clientHandle, address, &device);
    if (err != ESP_OK) {
        Serial.printf("[USB] Could not open address %u: %s\n", address, esp_err_to_name(err));
        return;
    }

    const usb_device_desc_t* descriptor = nullptr;
    err = usb_host_get_device_descriptor(device, &descriptor);
    if (err != ESP_OK || !descriptor) {
        Serial.printf("[USB] Could not read the device descriptor: %s\n", esp_err_to_name(err));
        usb_host_device_close(_clientHandle, device);
        return;
    }

    Serial.printf(
        "[USB] Device address=%u VID=0x%04X PID=0x%04X class=0x%02X\n",
        address,
        descriptor->idVendor,
        descriptor->idProduct,
        descriptor->bDeviceClass
    );

    const usb_config_desc_t* configuration = nullptr;
    if (usb_host_get_active_config_descriptor(device, &configuration) == ESP_OK && configuration) {
        logConfiguration(configuration);
    }

    if (descriptor->idVendor != TONEX_USB_VID) {
        Serial.println("[USB] Device is not an IK Multimedia TONEX; closing it");
        usb_host_device_close(_clientHandle, device);
        return;
    }

    _deviceHandle = device;
    claimMidiInterface(configuration);
    _connected = true;
    Serial.printf("[USB] TONEX connected at address %u\n", address);
    if (_connCb) _connCb(true);
}

void ToneXUsbHost::handleDeviceGone(usb_device_handle_t device) {
    if (device != _deviceHandle) return;

    _syncing = false;
    _syncIndex = 0;
    _connected = false;
    _deviceHandle = nullptr;
    if (_midiInterfaceNumber != 0xFF) {
        usb_host_interface_release(_clientHandle, device, _midiInterfaceNumber);
        _midiInterfaceNumber = 0xFF;
        _midiAlternateSetting = 0;
        _midiEndpointOut = 0;
    }
    usb_host_device_close(_clientHandle, device);
    Serial.println("[USB] TONEX disconnected");
    if (_connCb) _connCb(false);
}

void ToneXUsbHost::logConfiguration(const usb_config_desc_t* config) const {
    Serial.printf(
        "[USB] Configuration value=%u interfaces=%u total_length=%u max_power_ma=%u\n",
        config->bConfigurationValue,
        config->bNumInterfaces,
        config->wTotalLength,
        config->bMaxPower * 2
    );

    const uint8_t* bytes = config->val;
    uint16_t offset = config->bLength;
    int currentInterface = -1;
    while (offset + 2 <= config->wTotalLength) {
        const usb_standard_desc_t* standard = reinterpret_cast<const usb_standard_desc_t*>(bytes + offset);
        if (standard->bLength < 2 || offset + standard->bLength > config->wTotalLength) break;

        if (standard->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE && standard->bLength >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t* interfaceDescriptor = reinterpret_cast<const usb_intf_desc_t*>(standard);
            currentInterface = interfaceDescriptor->bInterfaceNumber;
            Serial.printf(
                "[USB] Interface number=%u alt=%u class=0x%02X subclass=0x%02X protocol=0x%02X endpoints=%u\n",
                interfaceDescriptor->bInterfaceNumber,
                interfaceDescriptor->bAlternateSetting,
                interfaceDescriptor->bInterfaceClass,
                interfaceDescriptor->bInterfaceSubClass,
                interfaceDescriptor->bInterfaceProtocol,
                interfaceDescriptor->bNumEndpoints
            );
        } else if (standard->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && standard->bLength >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t* endpoint = reinterpret_cast<const usb_ep_desc_t*>(standard);
            Serial.printf(
                "[USB] Endpoint interface=%d address=0x%02X attributes=0x%02X max_packet=%u interval=%u\n",
                currentInterface,
                endpoint->bEndpointAddress,
                endpoint->bmAttributes,
                endpoint->wMaxPacketSize,
                endpoint->bInterval
            );
        }

        offset += standard->bLength;
    }
}

bool ToneXUsbHost::claimMidiInterface(const usb_config_desc_t* config) {
    if (!config) return false;

    const uint8_t* bytes = config->val;
    uint16_t offset = config->bLength;
    const usb_intf_desc_t* midiInterface = nullptr;
    uint8_t midiEndpointOut = 0;

    while (offset + 2 <= config->wTotalLength) {
        const usb_standard_desc_t* standard = reinterpret_cast<const usb_standard_desc_t*>(bytes + offset);
        if (standard->bLength < 2 || offset + standard->bLength > config->wTotalLength) break;

        if (standard->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE && standard->bLength >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t* candidate = reinterpret_cast<const usb_intf_desc_t*>(standard);
            if (candidate->bInterfaceClass == USB_CLASS_AUDIO && candidate->bInterfaceSubClass == 0x03) {
                midiInterface = candidate;
                midiEndpointOut = 0;
            } else {
                midiInterface = nullptr;
            }
        } else if (midiInterface && standard->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && standard->bLength >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t* endpoint = reinterpret_cast<const usb_ep_desc_t*>(standard);
            const bool isOut = (endpoint->bEndpointAddress & 0x80) == 0;
            const bool isBulk = (endpoint->bmAttributes & 0x03) == USB_TRANSFER_TYPE_BULK;
            if (isOut && isBulk) midiEndpointOut = endpoint->bEndpointAddress;
        }

        offset += standard->bLength;

        if (midiInterface && midiEndpointOut) {
            const esp_err_t err = usb_host_interface_claim(
                _clientHandle,
                _deviceHandle,
                midiInterface->bInterfaceNumber,
                midiInterface->bAlternateSetting
            );
            if (err != ESP_OK) {
                Serial.printf("[USB] Could not claim MIDI interface %u: %s\n", midiInterface->bInterfaceNumber, esp_err_to_name(err));
                return false;
            }

            _midiInterfaceNumber = midiInterface->bInterfaceNumber;
            _midiAlternateSetting = midiInterface->bAlternateSetting;
            _midiEndpointOut = midiEndpointOut;
            Serial.printf(
                "[USB] MIDI ready: interface=%u alt=%u endpoint_out=0x%02X\n",
                _midiInterfaceNumber,
                _midiAlternateSetting,
                _midiEndpointOut
            );
            return true;
        }
    }

    Serial.println("[USB] No USB-MIDI streaming interface with a bulk OUT endpoint was found");
    return false;
}

bool ToneXUsbHost::submitMidiPacket(const uint8_t packet[4]) {
    usb_transfer_t* transfer = nullptr;
    esp_err_t err = usb_host_transfer_alloc(4, 0, &transfer);
    if (err != ESP_OK || !transfer) {
        Serial.printf("[USB] Could not allocate MIDI transfer: %s\n", esp_err_to_name(err));
        return false;
    }

    std::memcpy(transfer->data_buffer, packet, 4);
    transfer->num_bytes = 4;
    transfer->device_handle = _deviceHandle;
    transfer->bEndpointAddress = _midiEndpointOut;
    transfer->callback = midiTransferCallback;
    transfer->context = this;

    err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        Serial.printf("[USB] Could not submit MIDI transfer: %s\n", esp_err_to_name(err));
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
}

void ToneXUsbHost::midiTransferCallback(usb_transfer_t* transfer) {
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED || transfer->actual_num_bytes != transfer->num_bytes) {
        Serial.printf(
            "[USB] MIDI transfer failed: status=%d bytes=%d/%d\n",
            transfer->status,
            transfer->actual_num_bytes,
            transfer->num_bytes
        );
    }
    usb_host_transfer_free(transfer);
}
#endif

bool ToneXUsbHost::isConnected() const {
    return _connected;
}

bool ToneXUsbHost::sendBankSelectAndPC(uint8_t bank, char slot, uint8_t channel) {
#ifdef NATIVE_TEST
    return false;
#else
    if (!_connected || !_deviceHandle || !_midiEndpointOut) {
        Serial.println("[USB] MIDI output is unavailable because its interface is not ready");
        return false;
    }

    const ToneXHDLC::UsbMidiPackets packets = ToneXHDLC::getUsbMidiPackets(bank, slot, channel);
    if (!submitMidiPacket(packets.bankSelect)) return false;
    delay(30);
    return submitMidiPacket(packets.programChange);
#endif
}

bool ToneXUsbHost::startSync() {
#ifndef NATIVE_TEST
    Serial.println("[USB] CDC preset sync is unavailable until the CDC interfaces are claimed");
#endif
    return false;
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
    (void)frame;
    return false;
}

std::vector<uint8_t> ToneXUsbHost::readCdcFrame(uint32_t timeoutMs) {
    (void)timeoutMs;
    return {};
}
