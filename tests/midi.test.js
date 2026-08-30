const { describe, it } = require('node:test');
const assert = require('node:assert');
const {
    TOTAL_BANKS,
    SLOTS,
    TOTAL_PRESETS,
    bankSlotFromPC,
    pcFromBankSlot,
    getMidiBankSelectAndPC
} = require('./helpers/tonex_protocol');

describe('MIDI Bank / Slot and Program Change Math', () => {
    it('calculates correct PC for first bank presets', () => {
        assert.strictEqual(pcFromBankSlot(0, 'A'), 0);
        assert.strictEqual(pcFromBankSlot(0, 'B'), 1);
        assert.strictEqual(pcFromBankSlot(0, 'C'), 2);
    });

    it('calculates correct PC for middle and high bank presets', () => {
        assert.strictEqual(pcFromBankSlot(42, 'C'), 128);
        assert.strictEqual(pcFromBankSlot(49, 'C'), 149);
    });

    it('reverses PC to Bank and Slot accurately across all 150 presets', () => {
        for (let pc = 0; pc < TOTAL_PRESETS; pc++) {
            const { bank, slot } = bankSlotFromPC(pc);
            const calculatedPc = pcFromBankSlot(bank, slot);
            assert.strictEqual(calculatedPc, pc, `Failed roundtrip at PC ${pc}`);
        }
    });

    it('generates correct MIDI Bank Select CC#0 = 0 for PC 0..127', () => {
        const midi0 = getMidiBankSelectAndPC(0, 'A', 0);
        assert.deepStrictEqual(midi0.bankSelect, [0xB0, 0x00, 0x00]);
        assert.deepStrictEqual(midi0.programChange, [0xC0, 0x00]);

        const midi127 = getMidiBankSelectAndPC(42, 'B', 0); // PC 127
        assert.deepStrictEqual(midi127.bankSelect, [0xB0, 0x00, 0x00]);
        assert.deepStrictEqual(midi127.programChange, [0xC0, 127]);
    });

    it('generates correct MIDI Bank Select CC#0 = 1 for PC 128..149', () => {
        const midi128 = getMidiBankSelectAndPC(42, 'C', 0); // PC 128
        assert.deepStrictEqual(midi128.bankSelect, [0xB0, 0x00, 0x01]);
        assert.deepStrictEqual(midi128.programChange, [0xC0, 0]); // 128 - 128 = 0

        const midi149 = getMidiBankSelectAndPC(49, 'C', 0); // PC 149
        assert.deepStrictEqual(midi149.bankSelect, [0xB0, 0x00, 0x01]);
        assert.deepStrictEqual(midi149.programChange, [0xC0, 21]); // 149 - 128 = 21
    });

    it('respects custom MIDI channels (0-15)', () => {
        const midiCh4 = getMidiBankSelectAndPC(0, 'A', 3); // MIDI Channel 4 (index 3)
        assert.deepStrictEqual(midiCh4.bankSelect, [0xB3, 0x00, 0x00]);
        assert.deepStrictEqual(midiCh4.programChange, [0xC3, 0x00]);
    });
});
