#include <unity.h>
#include "tonex_hdlc.h"

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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_ccitt_calculation);
    RUN_TEST(test_byte_stuffing);
    RUN_TEST(test_frame_build_and_deframe);
    RUN_TEST(test_deframe_corrupt_crc_rejection);
    RUN_TEST(test_midi_math_roundtrip);
    RUN_TEST(test_midi_bank_select_commands);
    return UNITY_END();
}
