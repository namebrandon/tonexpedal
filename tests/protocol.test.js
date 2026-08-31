const { describe, it } = require('node:test');
const assert = require('node:assert');
const {
    crcCCITT,
    stuffByte,
    buildFrame,
    deframe,
    extractAfterMarker,
    decodePresetName,
    readFloat32,
    createPresetRequest,
    HELLO_CMD,
    REQUEST_STATE_CMD,
    NAME_MARKER,
    PARAM_MARKER,
    AMP_ENABLE_INDEX,
    CAB_TYPE_INDEX,
    FLOAT_SIZE
} = require('./helpers/tonex_protocol');

describe('CRC-CCITT Calculation', () => {
    it('calculates expected CRC for Hello command payload', () => {
        const crc = crcCCITT(HELLO_CMD);
        assert.strictEqual(typeof crc, 'number');
        assert.ok(crc >= 0 && crc <= 0xFFFF);
    });

    it('calculates consistent CRC across multiple runs', () => {
        const payload = new Uint8Array([0x01, 0x02, 0x03, 0x04, 0x05]);
        const crc1 = crcCCITT(payload);
        const crc2 = crcCCITT(payload);
        assert.strictEqual(crc1, crc2);
    });

    it('produces different CRC when a single bit is flipped', () => {
        const p1 = new Uint8Array([0x01, 0x02, 0x03]);
        const p2 = new Uint8Array([0x01, 0x02, 0x02]);
        assert.notStrictEqual(crcCCITT(p1), crcCCITT(p2));
    });
});

describe('HDLC Byte Stuffing & Framing', () => {
    it('escapes 0x7E delimiter byte as 0x7D 0x5E', () => {
        const out = [];
        stuffByte(out, 0x7E);
        assert.deepStrictEqual(out, [0x7D, 0x5E]);
    });

    it('escapes 0x7D escape byte as 0x7D 0x5D', () => {
        const out = [];
        stuffByte(out, 0x7D);
        assert.deepStrictEqual(out, [0x7D, 0x5D]);
    });

    it('leaves standard bytes unescaped', () => {
        const out = [];
        stuffByte(out, 0x42);
        assert.deepStrictEqual(out, [0x42]);
    });

    it('builds a framed buffer starting and ending with 0x7E', () => {
        const frame = buildFrame(HELLO_CMD);
        assert.strictEqual(frame[0], 0x7E);
        assert.strictEqual(frame[frame.length - 1], 0x7E);
        assert.ok(frame.length >= HELLO_CMD.length + 4);
    });

    it('successfully deframes and verifies CRC of a built frame', () => {
        const payload = new Uint8Array([0x10, 0x20, 0x30, 0x7E, 0x7D, 0x40]);
        const framed = buildFrame(payload);
        const deframed = deframe(framed);

        assert.ok(deframed !== null);
        assert.deepStrictEqual(Array.from(deframed), Array.from(payload));
    });

    it('rejects frame with corrupt CRC', () => {
        const payload = new Uint8Array([0x01, 0x02, 0x03]);
        const framed = buildFrame(payload);
        // Corrupt a byte before the closing 0x7E
        framed[framed.length - 2] ^= 0xFF;
        const result = deframe(framed);
        assert.strictEqual(result, null);
    });

    it('rejects invalid or truncated frames', () => {
        assert.strictEqual(deframe(new Uint8Array([0x7E, 0x01, 0x7E])), null);
        assert.strictEqual(deframe(new Uint8Array([0x01, 0x02, 0x03, 0x04])), null);
    });
});

describe('ToneX Preset Request Commands', () => {
    it('creates standard 17-byte command for index < 128', () => {
        const req = createPresetRequest(42);
        assert.strictEqual(req.length, 17);
        assert.strictEqual(req[15], 42);
    });

    it('creates 18-byte escape command with 0x80 for index >= 128', () => {
        const req = createPresetRequest(130);
        assert.strictEqual(req.length, 18);
        assert.strictEqual(req[15], 0x80);
        assert.strictEqual(req[16], 130);
    });
});

describe('ToneX Binary Marker & Preset Decoding', () => {
    it('uses the parameter marker observed on a full-size TONEX Pedal', () => {
        assert.deepStrictEqual(Array.from(PARAM_MARKER), [0xBA, 0x03, 0xBA, 0x29]);
    });

    it('extracts bytes following a specified marker', () => {
        const data = new Uint8Array([0x01, 0x02, 0xB9, 0x04, 0xB9, 0x02, 0xBC, 0x21, 0x50, 0x6C, 0x65, 0x78, 0x69]);
        const extracted = extractAfterMarker(data, NAME_MARKER);
        assert.ok(extracted !== null);
        assert.strictEqual(decodePresetName(extracted), 'Plexi');
    });

    it('decodes UTF-8 preset names with null bytes and accents', () => {
        const encoder = new TextEncoder();
        const raw = new Uint8Array(32);
        const nameBytes = encoder.encode('80s Solo Lead é');
        raw.set(nameBytes, 0);

        const decoded = decodePresetName(raw);
        assert.strictEqual(decoded, '80s Solo Lead é');
    });

    it('parses IEEE 754 float32 values for AMP & CAB states', () => {
        const buffer = new Uint8Array(150);
        const view = new DataView(buffer.buffer);

        const ampOffset = AMP_ENABLE_INDEX * FLOAT_SIZE;
        buffer[ampOffset] = 0x88; // Float marker
        view.setFloat32(ampOffset + 1, 1.0, true); // 1.0 = AMP Enabled

        const cabOffset = CAB_TYPE_INDEX * FLOAT_SIZE;
        buffer[cabOffset] = 0x88; // Float marker
        view.setFloat32(cabOffset + 1, 0.0, true); // 0.0 = CAB Disabled

        const ampVal = readFloat32(buffer, ampOffset + 1);
        const cabVal = readFloat32(buffer, cabOffset + 1);

        assert.strictEqual(ampVal > 0.5, true);
        assert.strictEqual(cabVal > 0.5, false);
    });
});
