'use strict';

const { deframe } = require('../tests/helpers/tonex_protocol');

function hexToBytes(hex) {
    if (typeof hex !== 'string') throw new TypeError('Hex data must be a string');
    const compact = hex.trim();
    if (compact === '') return new Uint8Array();

    const parts = compact.split(/\s+/);
    if (parts.some(part => !/^[0-9a-f]{2}$/i.test(part))) {
        throw new Error('Invalid space-separated hexadecimal data');
    }
    return Uint8Array.from(parts.map(part => parseInt(part, 16)));
}

function validateDiagnosticCapture(capture) {
    if (!capture || typeof capture !== 'object' || Array.isArray(capture)) {
        throw new Error('Diagnostic capture must be an object');
    }
    if (capture.schema_version !== 1) {
        throw new Error(`Unsupported diagnostic schema version: ${capture.schema_version}`);
    }
    if (capture.application !== 'tonexpedal') {
        throw new Error('Capture was not produced by tonexpedal');
    }
    if (!Array.isArray(capture.events)) {
        throw new Error('Diagnostic capture events must be an array');
    }
    return capture;
}

class HdlcCaptureStream {
    constructor() {
        this.pendingFrame = [];
    }

    push(chunk) {
        const payloads = [];
        for (const byte of chunk) {
            if (byte === 0x7E) {
                if (this.pendingFrame.length > 1) {
                    this.pendingFrame.push(byte);
                    const payload = deframe(Uint8Array.from(this.pendingFrame));
                    if (payload) payloads.push(payload);
                }
                this.pendingFrame = [0x7E];
            } else if (this.pendingFrame.length > 0) {
                this.pendingFrame.push(byte);
            }
        }
        return payloads;
    }
}

function reconstructHdlcPayloads(capture, transport) {
    validateDiagnosticCapture(capture);
    const decoder = new HdlcCaptureStream();
    const payloads = [];

    for (const event of capture.events) {
        if (event.type !== 'transport_rx_chunk') continue;
        if (transport && event.transport !== transport) continue;
        payloads.push(...decoder.push(hexToBytes(event.bytes_hex)));
    }
    return payloads;
}

function summarizeDiagnosticCapture(capture) {
    validateDiagnosticCapture(capture);
    const eventCounts = {};
    const transports = new Set();

    for (const event of capture.events) {
        eventCounts[event.type] = (eventCounts[event.type] || 0) + 1;
        if (event.transport) transports.add(event.transport);
    }

    return {
        schema_version: capture.schema_version,
        application_version: capture.application_version || null,
        started_at: capture.started_at || null,
        transports: Array.from(transports).sort(),
        total_events: capture.events.length,
        event_counts: eventCounts,
        reconstructed_hdlc_frames: reconstructHdlcPayloads(capture).length,
        recorded_hdlc_frames: eventCounts.hdlc_rx_frame || 0,
        crc_errors: eventCounts.hdlc_crc_error || 0,
        read_timeouts: eventCounts.hdlc_read_timeout || 0
    };
}

module.exports = {
    HdlcCaptureStream,
    hexToBytes,
    reconstructHdlcPayloads,
    summarizeDiagnosticCapture,
    validateDiagnosticCapture
};
