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
    : _connected(false), _syncing(false), _syncIndex(0), _lastSyncStepMs(0), _activePreset(-1)
#ifndef NATIVE_TEST
    , _hostInstalled(false), _clientHandle(nullptr), _deviceHandle(nullptr), _goneDeviceHandle(nullptr), _libraryTaskHandle(nullptr),
      _midiInterfaceNumber(0xFF), _midiAlternateSetting(0), _midiEndpointOut(0),
      _cdcControlInterfaceNumber(0xFF), _cdcControlAlternateSetting(0),
      _cdcDataInterfaceNumber(0xFF), _cdcDataAlternateSetting(0),
      _cdcEndpointIn(0), _cdcEndpointOut(0), _cdcEndpointInMaxPacket(0),
      _cdcReady(false), _cdcRxStream(nullptr), _cdcInTransfer(nullptr), _syncTaskHandle(nullptr),
      _midiTransfersInFlight(0), _cdcOutTransfersInFlight(0), _cdcInsideFrame(false)
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

    _cdcRxStream = xStreamBufferCreate(4096, 1);
    if (!_cdcRxStream) {
        Serial.println("[USB] Could not allocate the CDC receive stream");
        usb_host_uninstall();
        _hostInstalled = false;
        return false;
    }
    _cdcFrameBuffer.reserve(1536);

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
        vStreamBufferDelete(_cdcRxStream);
        _cdcRxStream = nullptr;
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
        vStreamBufferDelete(_cdcRxStream);
        _cdcRxStream = nullptr;
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
    cleanupGoneDevice();
    if (_connected && _cdcReady && !_syncing && !_syncTaskHandle) {
        for (uint8_t count = 0; count < 4; count++) {
            const std::vector<uint8_t> payload = readCdcFrame(0, false);
            if (payload.empty()) break;
            handleCdcEvent(payload);
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
    _activePreset = -1;
    claimMidiInterface(configuration);
    claimCdcInterfaces(configuration);
    _connected = true;
    Serial.printf("[USB] TONEX connected at address %u\n", address);
    if (_connCb) _connCb(true);
}

void ToneXUsbHost::handleDeviceGone(usb_device_handle_t device) {
    if (device != _deviceHandle) return;

    const bool syncWasActive = _syncing;
    _syncing = false;
    _syncIndex = 0;
    _connected = false;
    _activePreset = -1;
    _cdcReady = false;
    _goneDeviceHandle = device;
    Serial.println("[USB] TONEX disconnected");
    if (_connCb) _connCb(false);
    if (syncWasActive && _syncErrorCb) _syncErrorCb("The TONEX disconnected during preset synchronization");
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
    if (!_connected || !_deviceHandle || !_midiEndpointOut) return false;

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

    _midiTransfersInFlight++;
    err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        _midiTransfersInFlight--;
        Serial.printf("[USB] Could not submit MIDI transfer: %s\n", esp_err_to_name(err));
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
}

void ToneXUsbHost::midiTransferCallback(usb_transfer_t* transfer) {
    ToneXUsbHost* host = static_cast<ToneXUsbHost*>(transfer->context);
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED || transfer->actual_num_bytes != transfer->num_bytes) {
        Serial.printf(
            "[USB] MIDI transfer failed: status=%d bytes=%d/%d\n",
            transfer->status,
            transfer->actual_num_bytes,
            transfer->num_bytes
        );
    }
    if (host->_midiTransfersInFlight > 0) host->_midiTransfersInFlight--;
    usb_host_transfer_free(transfer);
}

bool ToneXUsbHost::claimCdcInterfaces(const usb_config_desc_t* config) {
    if (!config) return false;

    struct DataCandidate {
        uint8_t interfaceNumber = 0xFF;
        uint8_t alternateSetting = 0;
        uint8_t endpointIn = 0;
        uint8_t endpointOut = 0;
        uint16_t endpointInMaxPacket = 0;
        bool preferred = false;
    } selected;

    uint8_t controlInterface = 0xFF;
    uint8_t controlAlternate = 0;
    const usb_intf_desc_t* current = nullptr;
    DataCandidate currentData;

    const auto finishCurrentInterface = [&]() {
        if (!current || !currentData.endpointIn || !currentData.endpointOut) return;
        const bool isMidi = current->bInterfaceClass == USB_CLASS_AUDIO && current->bInterfaceSubClass == 0x03;
        if (isMidi) return;

        const bool preferred = current->bInterfaceClass == USB_CLASS_CDC_DATA;
        if (selected.interfaceNumber == 0xFF || (preferred && !selected.preferred)) {
            currentData.interfaceNumber = current->bInterfaceNumber;
            currentData.alternateSetting = current->bAlternateSetting;
            currentData.preferred = preferred;
            selected = currentData;
        }
    };

    const uint8_t* bytes = config->val;
    uint16_t offset = config->bLength;
    while (offset + 2 <= config->wTotalLength) {
        const usb_standard_desc_t* standard = reinterpret_cast<const usb_standard_desc_t*>(bytes + offset);
        if (standard->bLength < 2 || offset + standard->bLength > config->wTotalLength) break;

        if (standard->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE && standard->bLength >= sizeof(usb_intf_desc_t)) {
            finishCurrentInterface();
            current = reinterpret_cast<const usb_intf_desc_t*>(standard);
            currentData = DataCandidate();
            if (current->bInterfaceClass == USB_CLASS_COMM && controlInterface == 0xFF) {
                controlInterface = current->bInterfaceNumber;
                controlAlternate = current->bAlternateSetting;
            }
        } else if (current && standard->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && standard->bLength >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t* endpoint = reinterpret_cast<const usb_ep_desc_t*>(standard);
            const bool isBulk = (endpoint->bmAttributes & 0x03) == USB_TRANSFER_TYPE_BULK;
            if (isBulk && (endpoint->bEndpointAddress & 0x80)) {
                currentData.endpointIn = endpoint->bEndpointAddress;
                currentData.endpointInMaxPacket = endpoint->wMaxPacketSize;
            } else if (isBulk) {
                currentData.endpointOut = endpoint->bEndpointAddress;
            }
        }

        offset += standard->bLength;
    }
    finishCurrentInterface();

    if (selected.interfaceNumber == 0xFF) {
        Serial.println("[USB] No CDC data interface with bulk IN and OUT endpoints was found");
        return false;
    }

    if (controlInterface != 0xFF) {
        esp_err_t err = usb_host_interface_claim(_clientHandle, _deviceHandle, controlInterface, controlAlternate);
        if (err != ESP_OK) {
            Serial.printf("[USB] Could not claim CDC control interface %u: %s\n", controlInterface, esp_err_to_name(err));
            return false;
        }
    }

    if (selected.interfaceNumber != controlInterface) {
        const esp_err_t err = usb_host_interface_claim(
            _clientHandle,
            _deviceHandle,
            selected.interfaceNumber,
            selected.alternateSetting
        );
        if (err != ESP_OK) {
            Serial.printf("[USB] Could not claim CDC data interface %u: %s\n", selected.interfaceNumber, esp_err_to_name(err));
            if (controlInterface != 0xFF) usb_host_interface_release(_clientHandle, _deviceHandle, controlInterface);
            return false;
        }
    }

    _cdcControlInterfaceNumber = controlInterface;
    _cdcControlAlternateSetting = controlAlternate;
    _cdcDataInterfaceNumber = selected.interfaceNumber;
    _cdcDataAlternateSetting = selected.alternateSetting;
    _cdcEndpointIn = selected.endpointIn;
    _cdcEndpointOut = selected.endpointOut;
    _cdcEndpointInMaxPacket = selected.endpointInMaxPacket;

    Serial.printf(
        "[USB] CDC claimed: control=%d data=%u endpoint_in=0x%02X endpoint_out=0x%02X max_packet=%u\n",
        _cdcControlInterfaceNumber == 0xFF ? -1 : _cdcControlInterfaceNumber,
        _cdcDataInterfaceNumber,
        _cdcEndpointIn,
        _cdcEndpointOut,
        _cdcEndpointInMaxPacket
    );

    if (_cdcRxStream) xStreamBufferReset(_cdcRxStream);
    _cdcFrameBuffer.clear();
    _cdcInsideFrame = false;
    if (_cdcControlInterfaceNumber != 0xFF) {
        return submitCdcControlRequest(0x20); // SET_LINE_CODING
    }

    _cdcReady = submitCdcRead();
    return _cdcReady;
}

bool ToneXUsbHost::submitCdcControlRequest(uint8_t request) {
    const bool lineCoding = request == 0x20;
    const size_t payloadLength = lineCoding ? 7 : 0;
    usb_transfer_t* transfer = nullptr;
    esp_err_t err = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + payloadLength, 0, &transfer);
    if (err != ESP_OK || !transfer) {
        Serial.printf("[USB] Could not allocate CDC control transfer: %s\n", esp_err_to_name(err));
        return false;
    }

    usb_setup_packet_t* setup = reinterpret_cast<usb_setup_packet_t*>(transfer->data_buffer);
    setup->bmRequestType = 0x21;
    setup->bRequest = request;
    setup->wValue = lineCoding ? 0 : 0x0003;
    setup->wIndex = _cdcControlInterfaceNumber;
    setup->wLength = payloadLength;

    if (lineCoding) {
        static const uint8_t coding[7] = {0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08};
        std::memcpy(transfer->data_buffer + sizeof(usb_setup_packet_t), coding, sizeof(coding));
    }

    transfer->num_bytes = sizeof(usb_setup_packet_t) + payloadLength;
    transfer->device_handle = _deviceHandle;
    transfer->bEndpointAddress = 0;
    transfer->callback = cdcControlTransferCallback;
    transfer->context = this;

    err = usb_host_transfer_submit_control(_clientHandle, transfer);
    if (err != ESP_OK) {
        Serial.printf("[USB] Could not submit CDC control request 0x%02X: %s\n", request, esp_err_to_name(err));
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
}

void ToneXUsbHost::cdcControlTransferCallback(usb_transfer_t* transfer) {
    ToneXUsbHost* host = static_cast<ToneXUsbHost*>(transfer->context);
    const usb_setup_packet_t* setup = reinterpret_cast<const usb_setup_packet_t*>(transfer->data_buffer);
    const uint8_t request = setup->bRequest;
    const bool completed = transfer->status == USB_TRANSFER_STATUS_COMPLETED;
    if (!completed) {
        Serial.printf("[USB] CDC control request 0x%02X failed with status=%d\n", request, transfer->status);
    }
    usb_host_transfer_free(transfer);

    if (!completed || !host->_connected || !host->_deviceHandle) return;
    if (request == 0x20) {
        host->submitCdcControlRequest(0x22); // SET_CONTROL_LINE_STATE
    } else if (request == 0x22) {
        host->_cdcReady = host->submitCdcRead();
        if (host->_cdcReady) Serial.println("[USB] CDC transport ready");
    }
}

bool ToneXUsbHost::submitCdcRead() {
    if (!_deviceHandle || !_cdcEndpointIn || !_cdcEndpointInMaxPacket) return false;

    if (!_cdcInTransfer) {
        const esp_err_t err = usb_host_transfer_alloc(_cdcEndpointInMaxPacket, 0, &_cdcInTransfer);
        if (err != ESP_OK || !_cdcInTransfer) {
            Serial.printf("[USB] Could not allocate CDC IN transfer: %s\n", esp_err_to_name(err));
            return false;
        }
    }

    _cdcInTransfer->num_bytes = _cdcEndpointInMaxPacket;
    _cdcInTransfer->device_handle = _deviceHandle;
    _cdcInTransfer->bEndpointAddress = _cdcEndpointIn;
    _cdcInTransfer->callback = cdcInTransferCallback;
    _cdcInTransfer->context = this;

    const esp_err_t err = usb_host_transfer_submit(_cdcInTransfer);
    if (err != ESP_OK) {
        Serial.printf("[USB] Could not submit CDC IN transfer: %s\n", esp_err_to_name(err));
        usb_host_transfer_free(_cdcInTransfer);
        _cdcInTransfer = nullptr;
        return false;
    }
    return true;
}

void ToneXUsbHost::cdcInTransferCallback(usb_transfer_t* transfer) {
    ToneXUsbHost* host = static_cast<ToneXUsbHost*>(transfer->context);
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        if (transfer->actual_num_bytes > 0 && host->_cdcRxStream) {
            const size_t written = xStreamBufferSend(
                host->_cdcRxStream,
                transfer->data_buffer,
                transfer->actual_num_bytes,
                0
            );
            if (written != static_cast<size_t>(transfer->actual_num_bytes)) {
                Serial.println("[USB] CDC receive stream overflow");
            }
        }
        if (host->_deviceHandle && host->_cdcReady) {
            transfer->num_bytes = host->_cdcEndpointInMaxPacket;
            const esp_err_t err = usb_host_transfer_submit(transfer);
            if (err == ESP_OK) return;
            Serial.printf("[USB] Could not resubmit CDC IN transfer: %s\n", esp_err_to_name(err));
        }
    } else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE && transfer->status != USB_TRANSFER_STATUS_CANCELED) {
        Serial.printf("[USB] CDC IN transfer failed with status=%d\n", transfer->status);
    }

    host->_cdcReady = false;
    host->_cdcInTransfer = nullptr;
    usb_host_transfer_free(transfer);
}

void ToneXUsbHost::cdcOutTransferCallback(usb_transfer_t* transfer) {
    ToneXUsbHost* host = static_cast<ToneXUsbHost*>(transfer->context);
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED || transfer->actual_num_bytes != transfer->num_bytes) {
        Serial.printf(
            "[USB] CDC OUT transfer failed: status=%d bytes=%d/%d\n",
            transfer->status,
            transfer->actual_num_bytes,
            transfer->num_bytes
        );
    }
    if (host->_cdcOutTransfersInFlight > 0) host->_cdcOutTransfersInFlight--;
    usb_host_transfer_free(transfer);
}

void ToneXUsbHost::resetClaimedInterfaces() {
    _midiInterfaceNumber = 0xFF;
    _midiAlternateSetting = 0;
    _midiEndpointOut = 0;
    _cdcControlInterfaceNumber = 0xFF;
    _cdcControlAlternateSetting = 0;
    _cdcDataInterfaceNumber = 0xFF;
    _cdcDataAlternateSetting = 0;
    _cdcEndpointIn = 0;
    _cdcEndpointOut = 0;
    _cdcEndpointInMaxPacket = 0;
    _cdcReady = false;
    _cdcFrameBuffer.clear();
    _cdcInsideFrame = false;
}

void ToneXUsbHost::cleanupGoneDevice() {
    if (!_goneDeviceHandle || _cdcInTransfer || _midiTransfersInFlight > 0 || _cdcOutTransfersInFlight > 0) return;

    const usb_device_handle_t device = _goneDeviceHandle;
    const auto releaseInterface = [this, device](uint8_t interfaceNumber, const char* label) {
        if (interfaceNumber == 0xFF) return true;
        const esp_err_t err = usb_host_interface_release(_clientHandle, device, interfaceNumber);
        if (err != ESP_OK) {
            Serial.printf("[USB] Waiting to release %s interface %u: %s\n", label, interfaceNumber, esp_err_to_name(err));
            return false;
        }
        return true;
    };

    if (!releaseInterface(_midiInterfaceNumber, "MIDI")) return;
    const uint8_t releasedMidi = _midiInterfaceNumber;
    _midiInterfaceNumber = 0xFF;

    if (_cdcDataInterfaceNumber != releasedMidi) {
        if (!releaseInterface(_cdcDataInterfaceNumber, "CDC data")) return;
    }
    const uint8_t releasedData = _cdcDataInterfaceNumber;
    _cdcDataInterfaceNumber = 0xFF;

    if (_cdcControlInterfaceNumber != releasedMidi && _cdcControlInterfaceNumber != releasedData) {
        if (!releaseInterface(_cdcControlInterfaceNumber, "CDC control")) return;
    }
    _cdcControlInterfaceNumber = 0xFF;

    const esp_err_t closeResult = usb_host_device_close(_clientHandle, device);
    if (closeResult != ESP_OK) {
        Serial.printf("[USB] Waiting to close disconnected TONEX: %s\n", esp_err_to_name(closeResult));
        return;
    }

    _deviceHandle = nullptr;
    _goneDeviceHandle = nullptr;
    resetClaimedInterfaces();
    if (_cdcRxStream) xStreamBufferReset(_cdcRxStream);
    Serial.println("[USB] Disconnected TONEX resources released");
}

void ToneXUsbHost::syncTask(void* arg) {
    ToneXUsbHost* host = static_cast<ToneXUsbHost*>(arg);
    host->runSync();
    host->_syncTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

void ToneXUsbHost::runSync() {
    const auto sendPayload = [this](const uint8_t* payload, size_t length) {
        const std::vector<uint8_t> frame = ToneXHDLC::buildFrame(payload, length);
        return sendCdcFrame(frame);
    };

    if (_cdcRxStream) xStreamBufferReset(_cdcRxStream);
    _cdcFrameBuffer.clear();
    _cdcInsideFrame = false;

    if (!sendPayload(ToneXHDLC::HELLO_CMD, sizeof(ToneXHDLC::HELLO_CMD))) {
        failSync("Unable to send the TONEX hello command");
        return;
    }
    if (readCdcResponse(3000).empty()) {
        if (_syncing) failSync("The TONEX did not answer the hello command");
        return;
    }

    delay(200);
    if (!_syncing) return;
    if (!sendPayload(ToneXHDLC::REQUEST_STATE_CMD, sizeof(ToneXHDLC::REQUEST_STATE_CMD))) {
        failSync("Unable to request the TONEX state");
        return;
    }
    if (readCdcResponse(3000).empty()) {
        if (_syncing) failSync("The TONEX did not answer the state request");
        return;
    }

    delay(200);
    for (uint16_t index = 0; index < TONEX_TOTAL_PRESETS && _syncing; index++) {
        const std::vector<uint8_t> request = ToneXHDLC::createPresetRequest(static_cast<uint8_t>(index));
        if (!sendPayload(request.data(), request.size())) {
            failSync("Unable to request preset " + std::to_string(index));
            return;
        }

        const std::vector<uint8_t> response = readCdcResponse(3000);
        if (!_syncing) return;
        if (response.empty()) {
            failSync("Timed out waiting for preset " + std::to_string(index));
            return;
        }

        uint8_t responseIndex = 0;
        if (!ToneXHDLC::decodePresetResponseIndex(response.data(), response.size(), responseIndex) || responseIndex != index) {
            failSync("Received the wrong response for preset " + std::to_string(index));
            return;
        }

        ToneXHDLC::PresetData preset;
        if (!ToneXHDLC::decodePresetResponse(response.data(), response.size(), preset)) {
            failSync("Could not decode preset " + std::to_string(index));
            return;
        }

        const ToneXHDLC::BankSlot location = ToneXHDLC::bankSlotFromPC(static_cast<uint8_t>(index));
        ToneXPresetInfo info;
        info.bank = location.bank;
        info.slot = location.slot;
        info.name = preset.name;
        info.amp = preset.amp;
        info.cab = preset.cab;
        if (_presetCb) _presetCb(info);

        _syncIndex = static_cast<uint8_t>(index + 1);
        if (_progCb) _progCb(_syncIndex, TONEX_TOTAL_PRESETS);
        delay(40);
    }

    if (!_syncing) return;
    _syncing = false;
    _syncIndex = 0;
    if (_completeCb) _completeCb(TONEX_TOTAL_PRESETS);
}

void ToneXUsbHost::failSync(const std::string& message) {
    _syncing = false;
    _syncIndex = 0;
    Serial.printf("[USB] Preset sync failed: %s\n", message.c_str());
    if (_syncErrorCb) _syncErrorCb(message);
}

void ToneXUsbHost::handleCdcEvent(const std::vector<uint8_t>& payload) {
    uint8_t presetIndex = 0;
    if (!ToneXHDLC::decodeActivePresetEvent(payload.data(), payload.size(), presetIndex)) return;

    _activePreset = presetIndex;
    ToneXHDLC::PresetData preset;
    if (ToneXHDLC::decodePresetResponse(payload.data(), payload.size(), preset)) {
        const ToneXHDLC::BankSlot location = ToneXHDLC::bankSlotFromPC(presetIndex);
        ToneXPresetInfo info;
        info.bank = location.bank;
        info.slot = location.slot;
        info.name = preset.name;
        info.amp = preset.amp;
        info.cab = preset.cab;
        if (_presetCb) _presetCb(info);
    }
    Serial.printf("[USB] Active preset event: index=%u\n", presetIndex);
    if (_activePresetCb) _activePresetCb(presetIndex);
}

std::vector<uint8_t> ToneXUsbHost::readCdcResponse(uint32_t timeoutMs) {
    const uint32_t startedAt = millis();
    while (_connected && _syncing) {
        const uint32_t elapsed = millis() - startedAt;
        if (elapsed >= timeoutMs) break;
        const uint32_t remaining = timeoutMs - elapsed;
        std::vector<uint8_t> payload = readCdcFrame(remaining);
        if (payload.empty()) return {};

        uint8_t presetIndex = 0;
        if (ToneXHDLC::decodeActivePresetEvent(payload.data(), payload.size(), presetIndex)) {
            handleCdcEvent(payload);
            continue;
        }
        return payload;
    }
    return {};
}
#endif

bool ToneXUsbHost::isConnected() const {
    return _connected;
}

int16_t ToneXUsbHost::activePreset() const {
    return _activePreset;
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

bool ToneXUsbHost::sendControlChange(uint8_t control, uint8_t value, uint8_t channel) {
#ifdef NATIVE_TEST
    (void)control;
    (void)value;
    (void)channel;
    return false;
#else
    if (!_connected || !_deviceHandle || !_midiEndpointOut) {
        Serial.println("[USB] MIDI output is unavailable because its interface is not ready");
        return false;
    }

    const ToneXHDLC::UsbMidiControlChange message =
        ToneXHDLC::getUsbMidiControlChange(control, value, channel);
    return submitMidiPacket(message.packet);
#endif
}

bool ToneXUsbHost::startSync() {
#ifdef NATIVE_TEST
    return false;
#else
    if (_syncing || !_connected || !_cdcReady || _syncTaskHandle) return false;

    _syncing = true;
    _syncIndex = 0;
    const BaseType_t created = xTaskCreate(
        syncTask,
        "tonex-sync",
        8192,
        this,
        1,
        &_syncTaskHandle
    );
    if (created != pdPASS) {
        _syncing = false;
        _syncTaskHandle = nullptr;
        Serial.println("[USB] Could not create the preset sync task");
        return false;
    }
    return true;
#endif
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

void ToneXUsbHost::onSyncError(SyncErrorCallback cb) {
    _syncErrorCb = cb;
}

void ToneXUsbHost::onPresetReceived(PresetReceivedCallback cb) {
    _presetCb = cb;
}

void ToneXUsbHost::onActivePresetChange(ActivePresetCallback cb) {
    _activePresetCb = cb;
}

bool ToneXUsbHost::sendCdcFrame(const std::vector<uint8_t>& frame) {
#ifdef NATIVE_TEST
    (void)frame;
    return false;
#else
    if (!_connected || !_deviceHandle || !_cdcReady || !_cdcEndpointOut || frame.empty()) return false;

    usb_transfer_t* transfer = nullptr;
    esp_err_t err = usb_host_transfer_alloc(frame.size(), 0, &transfer);
    if (err != ESP_OK || !transfer) {
        Serial.printf("[USB] Could not allocate CDC OUT transfer: %s\n", esp_err_to_name(err));
        return false;
    }

    std::memcpy(transfer->data_buffer, frame.data(), frame.size());
    transfer->num_bytes = frame.size();
    transfer->device_handle = _deviceHandle;
    transfer->bEndpointAddress = _cdcEndpointOut;
    transfer->callback = cdcOutTransferCallback;
    transfer->context = this;

    _cdcOutTransfersInFlight++;
    err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        _cdcOutTransfersInFlight--;
        Serial.printf("[USB] Could not submit CDC OUT transfer: %s\n", esp_err_to_name(err));
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
#endif
}

std::vector<uint8_t> ToneXUsbHost::readCdcFrame(uint32_t timeoutMs, bool syncOnly) {
#ifdef NATIVE_TEST
    (void)timeoutMs;
    (void)syncOnly;
    return {};
#else
    if (!_cdcRxStream) return {};

    const uint32_t startedAt = millis();
    while (_connected && (!syncOnly || _syncing)) {
        if (timeoutMs > 0 && millis() - startedAt >= timeoutMs) break;
        uint8_t byte = 0;
        const TickType_t waitTicks = timeoutMs > 0 ? pdMS_TO_TICKS(50) : 0;
        const size_t received = xStreamBufferReceive(_cdcRxStream, &byte, 1, waitTicks);
        if (received == 0) {
            if (timeoutMs == 0) break;
            continue;
        }

        if (byte == 0x7E) {
            if (!_cdcInsideFrame) {
                _cdcFrameBuffer.clear();
                _cdcFrameBuffer.push_back(byte);
                _cdcInsideFrame = true;
            } else if (_cdcFrameBuffer.size() > 1) {
                _cdcFrameBuffer.push_back(byte);
                std::vector<uint8_t> payload = ToneXHDLC::deframe(
                    _cdcFrameBuffer.data(),
                    _cdcFrameBuffer.size()
                );
                _cdcFrameBuffer.assign(1, 0x7E);
                if (!payload.empty()) return payload;
            }
        } else if (_cdcInsideFrame) {
            _cdcFrameBuffer.push_back(byte);
            if (_cdcFrameBuffer.size() > 16384) {
                _cdcFrameBuffer.clear();
                _cdcInsideFrame = false;
            }
        }
    }
    return {};
#endif
}
