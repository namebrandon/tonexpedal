#include "tonex_hdlc.h"
#include <cstring>
#include <algorithm>

namespace ToneXHDLC {

    const uint8_t HELLO_CMD[13] = {
        0xb9, 0x03, 0x00, 0x82, 0x04, 0x00, 0x80, 0x10, 0x01, 0xb9, 0x02, 0x02, 0x10
    };

    const uint8_t REQUEST_STATE_CMD[15] = {
        0xb9, 0x03, 0x00, 0x82, 0x06, 0x00, 0x80, 0x10, 0x03, 0xb9, 0x02, 0x81, 0x01, 0x02, 0x10
    };

    const uint8_t NAME_MARKER[6] = {
        0xB9, 0x04, 0xB9, 0x02, 0xBC, 0x21
    };

    const uint8_t PARAM_MARKER[4] = {
        0xBA, 0x03, 0xBA, 0x6D
    };

    static const char SLOTS[3] = {'A', 'B', 'C'};

    uint16_t calculateCRC(const uint8_t* data, size_t length) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                crc = (crc & 1) ? ((crc >> 1) ^ 0x8408) : (crc >> 1);
            }
        }
        return (~crc) & 0xFFFF;
    }

    void stuffByte(std::vector<uint8_t>& output, uint8_t byte) {
        if (byte == 0x7E || byte == 0x7D) {
            output.push_back(0x7D);
            output.push_back(byte ^ 0x20);
        } else {
            output.push_back(byte);
        }
    }

    std::vector<uint8_t> buildFrame(const uint8_t* payload, size_t length) {
        std::vector<uint8_t> out;
        out.reserve(length + 8);
        out.push_back(0x7E);

        for (size_t i = 0; i < length; i++) {
            stuffByte(out, payload[i]);
        }

        uint16_t crc = calculateCRC(payload, length);
        stuffByte(out, crc & 0xFF);
        stuffByte(out, (crc >> 8) & 0xFF);
        out.push_back(0x7E);

        return out;
    }

    std::vector<uint8_t> deframe(const uint8_t* frameBytes, size_t length) {
        if (length < 4 || frameBytes[0] != 0x7E || frameBytes[length - 1] != 0x7E) {
            return {};
        }

        std::vector<uint8_t> unstuffed;
        unstuffed.reserve(length);

        for (size_t i = 1; i < length - 1; i++) {
            if (frameBytes[i] == 0x7D) {
                if (i + 1 < length - 1) {
                    unstuffed.push_back(frameBytes[i + 1] ^ 0x20);
                    i++;
                }
            } else if (frameBytes[i] == 0x7E) {
                break;
            } else {
                unstuffed.push_back(frameBytes[i]);
            }
        }

        if (unstuffed.size() < 2) {
            return {};
        }

        size_t payloadLen = unstuffed.size() - 2;
        uint16_t recvCrc = unstuffed[payloadLen] | (unstuffed[payloadLen + 1] << 8);
        uint16_t calcCrc = calculateCRC(unstuffed.data(), payloadLen);

        if (recvCrc != calcCrc) {
            return {};
        }

        return std::vector<uint8_t>(unstuffed.begin(), unstuffed.begin() + payloadLen);
    }

    std::vector<uint8_t> createPresetRequest(uint8_t index) {
        if (index < 128) {
            return {
                0xb9, 0x03, 0x81, 0x00, 0x02, 0x82, 0x06, 0x00, 0x80, 0x10, 0x03, 0xb9, 0x04, 0x10, 0x01, index, 0x00
            };
        } else {
            return {
                0xb9, 0x03, 0x81, 0x00, 0x02, 0x82, 0x06, 0x00, 0x80, 0x10, 0x03, 0xb9, 0x04, 0x10, 0x01, 0x80, index, 0x00
            };
        }
    }

    int findMarker(const uint8_t* data, size_t dataLen, const uint8_t* marker, size_t markerLen) {
        if (dataLen < markerLen) return -1;
        for (size_t i = 0; i <= dataLen - markerLen; i++) {
            if (std::memcmp(data + i, marker, markerLen) == 0) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::string decodePresetName(const uint8_t* nameBytes, size_t length) {
        std::string str;
        for (size_t i = 0; i < length; i++) {
            if (nameBytes[i] == 0) break;
            str.push_back(static_cast<char>(nameBytes[i]));
        }
        // Trim trailing spaces
        while (!str.empty() && (str.back() == ' ' || str.back() == '\r' || str.back() == '\n')) {
            str.pop_back();
        }
        return str;
    }

    float readFloat32(const uint8_t* bytes, size_t offset) {
        float val = 0.0f;
        std::memcpy(&val, bytes + offset, sizeof(float));
        return val;
    }

    bool decodePresetResponse(const uint8_t* data, size_t length, PresetData& preset) {
        constexpr size_t NAME_LENGTH = 32;
        constexpr size_t FLOAT_SIZE = 5;
        constexpr size_t AMP_ENABLE_INDEX = 17;
        constexpr size_t CAB_TYPE_INDEX = 22;

        preset = PresetData{"", false, false};
        const int nameMarkerOffset = findMarker(data, length, NAME_MARKER, sizeof(NAME_MARKER));
        if (nameMarkerOffset < 0) return false;

        const size_t nameOffset = static_cast<size_t>(nameMarkerOffset) + sizeof(NAME_MARKER);
        if (nameOffset + NAME_LENGTH > length) return false;
        preset.name = decodePresetName(data + nameOffset, NAME_LENGTH);
        if (preset.name.empty()) return false;

        const int parameterMarkerOffset = findMarker(data, length, PARAM_MARKER, sizeof(PARAM_MARKER));
        if (parameterMarkerOffset < 0) return true;

        const size_t parameterOffset = static_cast<size_t>(parameterMarkerOffset) + sizeof(PARAM_MARKER);
        const size_t ampOffset = parameterOffset + AMP_ENABLE_INDEX * FLOAT_SIZE;
        const size_t cabOffset = parameterOffset + CAB_TYPE_INDEX * FLOAT_SIZE;
        if (ampOffset + FLOAT_SIZE <= length && data[ampOffset] == 0x88) {
            preset.amp = readFloat32(data, ampOffset + 1) > 0.5f;
        }
        if (cabOffset + FLOAT_SIZE <= length && data[cabOffset] == 0x88) {
            preset.cab = readFloat32(data, cabOffset + 1) > 0.5f;
        }
        return true;
    }

    BankSlot bankSlotFromPC(uint8_t pc) {
        BankSlot bs;
        bs.bank = pc / 3;
        bs.slot = SLOTS[pc % 3];
        return bs;
    }

    uint8_t pcFromBankSlot(uint8_t bank, char slot) {
        uint8_t slotIdx = 0;
        if (slot == 'B') slotIdx = 1;
        else if (slot == 'C') slotIdx = 2;
        return bank * 3 + slotIdx;
    }

    MidiMessage getMidiBankSelectAndPC(uint8_t bank, char slot, uint8_t channel) {
        MidiMessage msg;
        uint8_t pc = pcFromBankSlot(bank, slot);
        uint8_t ch = channel & 0x0F;
        uint8_t midiCh = 0xB0 + ch;
        uint8_t pcCh = 0xC0 + ch;

        msg.pc = pc;
        if (pc > 127) {
            msg.bankSelect[0] = midiCh;
            msg.bankSelect[1] = 0x00;
            msg.bankSelect[2] = 0x01;
            msg.programChange[0] = pcCh;
            msg.programChange[1] = pc - 128;
        } else {
            msg.bankSelect[0] = midiCh;
            msg.bankSelect[1] = 0x00;
            msg.bankSelect[2] = 0x00;
            msg.programChange[0] = pcCh;
            msg.programChange[1] = pc;
        }
        return msg;
    }

    UsbMidiPackets getUsbMidiPackets(uint8_t bank, char slot, uint8_t channel) {
        const MidiMessage midi = getMidiBankSelectAndPC(bank, slot, channel);
        UsbMidiPackets packets = {};

        // USB-MIDI 1.0 event packets use cable 0 and a Code Index Number
        // matching the MIDI message type in the low nibble.
        packets.bankSelect[0] = 0x0B; // Control Change
        packets.bankSelect[1] = midi.bankSelect[0];
        packets.bankSelect[2] = midi.bankSelect[1];
        packets.bankSelect[3] = midi.bankSelect[2];

        packets.programChange[0] = 0x0C; // Program Change
        packets.programChange[1] = midi.programChange[0];
        packets.programChange[2] = midi.programChange[1];
        packets.programChange[3] = 0x00;
        return packets;
    }
}
