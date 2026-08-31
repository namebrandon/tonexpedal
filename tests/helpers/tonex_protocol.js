/**
 * TONEX Pedal Protocol helper functions for tests and utilities.
 * Exact logic mirrored from index.html for testability and cross-language parity.
 */

const TOTAL_BANKS = 50;
const SLOTS = ['A', 'B', 'C'];
const TOTAL_PRESETS = TOTAL_BANKS * 3; // 150

function crcCCITT(data) {
    let crc = 0xFFFF;
    for (let i = 0; i < data.length; i++) {
        crc ^= data[i];
        for (let j = 0; j < 8; j++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0x8408) : (crc >> 1);
        }
    }
    return ~crc & 0xFFFF;
}

function stuffByte(output, byte) {
    if (byte === 0x7E || byte === 0x7D) {
        output.push(0x7D, byte ^ 0x20);
    } else {
        output.push(byte);
    }
    return 1;
}

function buildFrame(payload) {
    const out = [0x7E];
    for (let i = 0; i < payload.length; i++) {
        stuffByte(out, payload[i]);
    }
    const crc = crcCCITT(payload);
    stuffByte(out, crc & 0xFF);
    stuffByte(out, (crc >> 8) & 0xFF);
    out.push(0x7E);
    return new Uint8Array(out);
}

function deframe(frameBytes) {
    if (frameBytes.length < 4 || frameBytes[0] !== 0x7E || frameBytes[frameBytes.length - 1] !== 0x7E) {
        return null;
    }
    const unstuffed = [];
    for (let i = 1; i < frameBytes.length - 1; i++) {
        if (frameBytes[i] === 0x7D) {
            if (i + 1 < frameBytes.length - 1) {
                unstuffed.push(frameBytes[i + 1] ^ 0x20);
                i++;
            }
        } else if (frameBytes[i] === 0x7E) {
            break;
        } else {
            unstuffed.push(frameBytes[i]);
        }
    }
    if (unstuffed.length < 2) return null;
    const payload = new Uint8Array(unstuffed.slice(0, unstuffed.length - 2));
    const recvCrc = unstuffed[unstuffed.length - 2] | (unstuffed[unstuffed.length - 1] << 8);
    const calcCrc = crcCCITT(payload);
    if (recvCrc !== calcCrc) {
        return null;
    }
    return payload;
}

function extractAfterMarker(data, marker) {
    for (let i = 0; i <= data.length - marker.length; i++) {
        let match = true;
        for (let j = 0; j < marker.length; j++) {
            if (data[i + j] !== marker[j]) { match = false; break; }
        }
        if (match) return data.slice(i + marker.length);
    }
    return null;
}

const _nameDecoder = new TextDecoder('utf-8');
function decodePresetName(data) {
    const str = _nameDecoder.decode(data);
    return str.replace(/\0/g, '').trim();
}

function readFloat32(bytes, offset) {
    if (offset + 4 > bytes.length) return null;
    const view = new DataView(new ArrayBuffer(4));
    view.setUint8(0, bytes[offset]);
    view.setUint8(1, bytes[offset + 1]);
    view.setUint8(2, bytes[offset + 2]);
    view.setUint8(3, bytes[offset + 3]);
    return view.getFloat32(0, true);
}

function bankSlotFromPC(pc) {
    if (pc < 127) {
        return { bank: Math.floor(pc / 3), slot: SLOTS[pc % 3] };
    } else {
        return { bank: Math.floor(pc / 3), slot: SLOTS[pc % 3] };
    }
}

function pcFromBankSlot(bank, slot) {
    const slotIndex = SLOTS.indexOf(slot);
    return bank * 3 + slotIndex;
}

function getMidiBankSelectAndPC(bank, slot, channel = 0) {
    const pc = pcFromBankSlot(bank, slot);
    const ch = channel & 0x0F;
    const midiCh = 0xB0 + ch;
    const pcCh = 0xC0 + ch;

    if (pc > 127) {
        return {
            bankSelect: [midiCh, 0x00, 0x01],
            programChange: [pcCh, pc - 128],
            pc: pc
        };
    } else {
        return {
            bankSelect: [midiCh, 0x00, 0x00],
            programChange: [pcCh, pc],
            pc: pc
        };
    }
}

const HELLO_CMD = new Uint8Array([0xb9, 0x03, 0x00, 0x82, 0x04, 0x00, 0x80, 0x10, 0x01, 0xb9, 0x02, 0x02, 0x10]);
const REQUEST_STATE_CMD = new Uint8Array([0xb9, 0x03, 0x00, 0x82, 0x06, 0x00, 0x80, 0x10, 0x03, 0xb9, 0x02, 0x81, 0x01, 0x02, 0x10]);
const NAME_MARKER = new Uint8Array([0xB9, 0x04, 0xB9, 0x02, 0xBC, 0x21]);
const PARAM_MARKER = new Uint8Array([0xBA, 0x03, 0xBA, 0x29]);
const AMP_ENABLE_INDEX = 17;
const CAB_TYPE_INDEX = 22;
const FLOAT_SIZE = 5;

function createPresetRequest(index) {
    if (index < 128) {
        return new Uint8Array([
            0xb9, 0x03, 0x81, 0x00, 0x02, 0x82, 0x06, 0x00, 0x80, 0x10, 0x03, 0xb9, 0x04, 0x10, 0x01, index, 0x00
        ]);
    } else {
        return new Uint8Array([
            0xb9, 0x03, 0x81, 0x00, 0x02, 0x82, 0x06, 0x00, 0x80, 0x10, 0x03, 0xb9, 0x04, 0x10, 0x01, 0x80, index, 0x00
        ]);
    }
}

module.exports = {
    TOTAL_BANKS,
    SLOTS,
    TOTAL_PRESETS,
    crcCCITT,
    stuffByte,
    buildFrame,
    deframe,
    extractAfterMarker,
    decodePresetName,
    readFloat32,
    bankSlotFromPC,
    pcFromBankSlot,
    getMidiBankSelectAndPC,
    createPresetRequest,
    HELLO_CMD,
    REQUEST_STATE_CMD,
    NAME_MARKER,
    PARAM_MARKER,
    AMP_ENABLE_INDEX,
    CAB_TYPE_INDEX,
    FLOAT_SIZE
};
