#include <unity.h>
#include "tonex_hdlc.h"
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

void test_crc_ccitt_calculation(void) {
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint16_t crc1 = ToneXHDLC::calculateCRC(payload, sizeof(payload));
    uint16_t crc2 = ToneXHDLC::calculateCRC(payload, sizeof(payload));
    TEST_ASSERT_EQUAL_HEX16(crc1, crc2);

    payload[0] ^= 0x01;
    uint16_t crc3 = ToneXHDLC::calculateCRC(payload, sizeof(payload));
    TEST_ASSERT_NOT_EQUAL(crc1, crc3);
}

void test_byte_stuffing(void) {
    std::vector<uint8_t> out;
    ToneXHDLC::stuffByte(out, 0x7E);
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
    TEST_ASSERT_EQUAL_HEX8(0x7D, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5E, out[1]);

    out.clear();
    ToneXHDLC::stuffByte(out, 0x7D);
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
    TEST_ASSERT_EQUAL_HEX8(0x7D, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5D, out[1]);

    out.clear();
    ToneXHDLC::stuffByte(out, 0x42);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_HEX8(0x42, out[0]);
}

void test_frame_build_and_deframe(void) {
    uint8_t payload[] = {0x10, 0x20, 0x30, 0x7E, 0x7D, 0x40};
    std::vector<uint8_t> frame = ToneXHDLC::buildFrame(payload, sizeof(payload));

    TEST_ASSERT_EQUAL_HEX8(0x7E, frame.front());
    TEST_ASSERT_EQUAL_HEX8(0x7E, frame.back());

    std::vector<uint8_t> deframed = ToneXHDLC::deframe(frame.data(), frame.size());
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), deframed.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, deframed.data(), sizeof(payload));
}

void test_deframe_corrupt_crc_rejection(void) {
    uint8_t payload[] = {0x01, 0x02, 0x03};
    std::vector<uint8_t> frame = ToneXHDLC::buildFrame(payload, sizeof(payload));

    // Corrupt second to last byte
    frame[frame.size() - 2] ^= 0xFF;
    std::vector<uint8_t> result = ToneXHDLC::deframe(frame.data(), frame.size());
    TEST_ASSERT_TRUE(result.empty());
}

void test_midi_math_roundtrip(void) {
    for (uint8_t pc = 0; pc < 150; pc++) {
        ToneXHDLC::BankSlot bs = ToneXHDLC::bankSlotFromPC(pc);
        uint8_t calculatedPc = ToneXHDLC::pcFromBankSlot(bs.bank, bs.slot);
        TEST_ASSERT_EQUAL_UINT8(pc, calculatedPc);
    }
}

void test_midi_bank_select_commands(void) {
    ToneXHDLC::MidiMessage m0 = ToneXHDLC::getMidiBankSelectAndPC(0, 'A', 0);
    TEST_ASSERT_EQUAL_HEX8(0xB0, m0.bankSelect[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, m0.bankSelect[2]); // CC#0 = 0
    TEST_ASSERT_EQUAL_HEX8(0xC0, m0.programChange[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, m0.programChange[1]);

    ToneXHDLC::MidiMessage m128 = ToneXHDLC::getMidiBankSelectAndPC(42, 'C', 0); // PC 128
    TEST_ASSERT_EQUAL_HEX8(0xB0, m128.bankSelect[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, m128.bankSelect[2]); // CC#0 = 1
    TEST_ASSERT_EQUAL_HEX8(0xC0, m128.programChange[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, m128.programChange[1]); // PC = 0
}

void test_usb_midi_event_packets(void) {
    ToneXHDLC::UsbMidiPackets packets = ToneXHDLC::getUsbMidiPackets(42, 'C', 3);

    const uint8_t expectedBank[] = {0x0B, 0xB3, 0x00, 0x01};
    const uint8_t expectedProgram[] = {0x0C, 0xC3, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedBank, packets.bankSelect, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedProgram, packets.programChange, 4);
}

void test_decode_preset_response(void) {
    std::vector<uint8_t> response(180, 0);
    const size_t nameMarkerOffset = 5;
    std::memcpy(response.data() + nameMarkerOffset, ToneXHDLC::NAME_MARKER, sizeof(ToneXHDLC::NAME_MARKER));
    const char* name = "Captured Plexi";
    std::memcpy(response.data() + nameMarkerOffset + sizeof(ToneXHDLC::NAME_MARKER), name, std::strlen(name));

    const size_t parameterMarkerOffset = 45;
    std::memcpy(response.data() + parameterMarkerOffset, ToneXHDLC::PARAM_MARKER, sizeof(ToneXHDLC::PARAM_MARKER));
    const size_t parameterOffset = parameterMarkerOffset + sizeof(ToneXHDLC::PARAM_MARKER);
    const float amp = 1.0f;
    const float cab = 2.0f;
    response[parameterOffset + 17 * 5] = 0x88;
    std::memcpy(response.data() + parameterOffset + 17 * 5 + 1, &amp, sizeof(amp));
    response[parameterOffset + 23 * 5] = 0x88;
    std::memcpy(response.data() + parameterOffset + 23 * 5 + 1, &cab, sizeof(cab));

    ToneXHDLC::PresetData preset;
    TEST_ASSERT_TRUE(ToneXHDLC::decodePresetResponse(response.data(), response.size(), preset));
    TEST_ASSERT_EQUAL_STRING("Captured Plexi", preset.name.c_str());
    TEST_ASSERT_TRUE(preset.amp);
    TEST_ASSERT_FALSE(preset.cab);

    const float toneModelCab = 0.0f;
    std::memcpy(response.data() + parameterOffset + 23 * 5 + 1, &toneModelCab, sizeof(toneModelCab));
    TEST_ASSERT_TRUE(ToneXHDLC::decodePresetResponse(response.data(), response.size(), preset));
    TEST_ASSERT_TRUE(preset.cab);
}

std::vector<uint8_t> presetIndexPayload(uint8_t index, bool unsolicited) {
    const bool extended = index >= 128;
    std::vector<uint8_t> response(extended ? 1190 : 1189, 0);
    const uint8_t header[] = {0xB9, 0x03, 0x81, 0x04, 0x02, 0x81, static_cast<uint8_t>(extended ? 0x9D : 0x9C)};
    const uint8_t responsePrefix[] = {0x04, 0x10, 0xB9, 0x03, 0x01};
    const uint8_t eventPrefix[] = {0x04, 0x02, 0xB9, 0x03, 0x00};
    std::memcpy(response.data(), header, sizeof(header));
    std::memcpy(response.data() + 7, unsolicited ? eventPrefix : responsePrefix, sizeof(responsePrefix));
    const size_t markerOffset = extended ? 14 : 13;
    if (extended) response[12] = 0x80;
    response[extended ? 13 : 12] = index;
    std::memcpy(response.data() + markerOffset, ToneXHDLC::NAME_MARKER, sizeof(ToneXHDLC::NAME_MARKER));
    return response;
}

void test_decode_preset_index_boundaries(void) {
    for (uint8_t index : {0, 127, 128, 149}) {
        for (bool unsolicited : {false, true}) {
            std::vector<uint8_t> response = presetIndexPayload(index, unsolicited);
            uint8_t decoded = 0xFF;
            TEST_ASSERT_TRUE(ToneXHDLC::decodePresetIndex(response.data(), response.size(), decoded));
            TEST_ASSERT_EQUAL_UINT8(index, decoded);
            if (unsolicited) {
                TEST_ASSERT_TRUE(ToneXHDLC::decodeActivePresetEvent(response.data(), response.size(), decoded));
                TEST_ASSERT_FALSE(ToneXHDLC::decodePresetResponseIndex(response.data(), response.size(), decoded));
            } else {
                TEST_ASSERT_TRUE(ToneXHDLC::decodePresetResponseIndex(response.data(), response.size(), decoded));
                TEST_ASSERT_FALSE(ToneXHDLC::decodeActivePresetEvent(response.data(), response.size(), decoded));
            }
        }
    }
}

void test_decode_preset_index_rejects_invalid_payloads(void) {
    std::vector<uint8_t> response = presetIndexPayload(128, true);
    response[13] = 150;
    uint8_t decoded = 0;
    TEST_ASSERT_FALSE(ToneXHDLC::decodePresetIndex(response.data(), response.size(), decoded));

    response = presetIndexPayload(128, true);
    response[13] = 42;
    TEST_ASSERT_FALSE(ToneXHDLC::decodePresetIndex(response.data(), response.size(), decoded));
}

void test_full_size_tonex_parameter_marker(void) {
    const uint8_t expected[] = {0xBA, 0x03, 0xBA, 0x29};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, ToneXHDLC::PARAM_MARKER, sizeof(expected));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_ccitt_calculation);
    RUN_TEST(test_byte_stuffing);
    RUN_TEST(test_frame_build_and_deframe);
    RUN_TEST(test_deframe_corrupt_crc_rejection);
    RUN_TEST(test_midi_math_roundtrip);
    RUN_TEST(test_midi_bank_select_commands);
    RUN_TEST(test_usb_midi_event_packets);
    RUN_TEST(test_decode_preset_response);
    RUN_TEST(test_decode_preset_index_boundaries);
    RUN_TEST(test_decode_preset_index_rejects_invalid_payloads);
    RUN_TEST(test_full_size_tonex_parameter_marker);
    return UNITY_END();
}
