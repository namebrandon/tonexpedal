#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>

namespace ToneXHDLC {

    // --- CRC-CCITT (poly 0x8408, init 0xFFFF, inverted output) ---
    uint16_t calculateCRC(const uint8_t* data, size_t length);

    // --- HDLC Byte Stuffing ---
    void stuffByte(std::vector<uint8_t>& output, uint8_t byte);

    // --- Frame Building & Deframing ---
    std::vector<uint8_t> buildFrame(const uint8_t* payload, size_t length);
    std::vector<uint8_t> deframe(const uint8_t* frameBytes, size_t length);

    // --- Commands & Markers ---
    extern const uint8_t HELLO_CMD[13];
    extern const uint8_t REQUEST_STATE_CMD[15];
    extern const uint8_t NAME_MARKER[6];
    extern const uint8_t PARAM_MARKER[4];

    std::vector<uint8_t> createPresetRequest(uint8_t index);

    // --- Parsing helpers ---
    int findMarker(const uint8_t* data, size_t dataLen, const uint8_t* marker, size_t markerLen);
    std::string decodePresetName(const uint8_t* nameBytes, size_t length);
    float readFloat32(const uint8_t* bytes, size_t offset);

    struct PresetData {
        std::string name;
        bool amp;
        bool cab;
    };

    bool decodePresetResponse(const uint8_t* data, size_t length, PresetData& preset);

    // --- MIDI Math helpers ---
    struct BankSlot {
        uint8_t bank;
        char slot; // 'A', 'B', 'C'
    };

    struct MidiMessage {
        uint8_t bankSelect[3];
        uint8_t programChange[2];
        uint8_t pc;
    };

    struct UsbMidiPackets {
        uint8_t bankSelect[4];
        uint8_t programChange[4];
    };

    BankSlot bankSlotFromPC(uint8_t pc);
    uint8_t pcFromBankSlot(uint8_t bank, char slot);
    MidiMessage getMidiBankSelectAndPC(uint8_t bank, char slot, uint8_t channel = 0);
    UsbMidiPackets getUsbMidiPackets(uint8_t bank, char slot, uint8_t channel = 0);
}
