const { describe, it } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const { buildFrame } = require('./helpers/tonex_protocol');
const {
    HdlcCaptureStream,
    hexToBytes,
    reconstructHdlcPayloads,
    summarizeDiagnosticCapture,
    validateDiagnosticCapture
} = require('../tools/diagnostic_capture');

const fixture = JSON.parse(fs.readFileSync(
    path.join(__dirname, 'fixtures', 'diagnostic_capture.synthetic.json'),
    'utf8'
));

describe('Diagnostic Capture Utilities', () => {
    it('validates a supported TONEX diagnostic capture', () => {
        assert.strictEqual(validateDiagnosticCapture(fixture), fixture);
        assert.throws(() => validateDiagnosticCapture([]), /must be an object/);
        assert.throws(() => validateDiagnosticCapture({ ...fixture, schema_version: 2 }), /Unsupported/);
    });

    it('rejects malformed hexadecimal fields', () => {
        assert.deepStrictEqual(Array.from(hexToBytes('7e 01 ff')), [0x7E, 0x01, 0xFF]);
        assert.throws(() => hexToBytes('7e xyz'), /Invalid/);
    });

    it('reassembles escaped frames split across arbitrary USB chunks', () => {
        const payloads = reconstructHdlcPayloads(fixture, 'webserial');
        assert.deepStrictEqual(payloads.map(payload => Array.from(payload)), [
            [0x01, 0x7E, 0x02],
            [0x10, 0x20, 0x30]
        ]);
    });

    it('recovers at the next delimiter after a corrupt frame', () => {
        const decoder = new HdlcCaptureStream();
        const corrupt = Uint8Array.from(buildFrame(Uint8Array.from([0xAA, 0xBB])));
        corrupt[2] ^= 0x01;
        const valid = buildFrame(Uint8Array.from([0x42]));
        const payloads = decoder.push(Uint8Array.from([...corrupt, ...valid]));
        assert.deepStrictEqual(payloads.map(payload => Array.from(payload)), [[0x42]]);
    });

    it('summarizes transport and framing outcomes without exposing payload data', () => {
        const summary = summarizeDiagnosticCapture(fixture);
        assert.deepStrictEqual(summary.transports, ['webserial']);
        assert.strictEqual(summary.total_events, 6);
        assert.strictEqual(summary.reconstructed_hdlc_frames, 2);
        assert.strictEqual(summary.read_timeouts, 0);
        assert.strictEqual('events' in summary, false);
    });
});
