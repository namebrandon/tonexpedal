const { describe, it } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const rootDir = path.join(__dirname, '..');

function loadTransition(relativePath) {
    const source = fs.readFileSync(path.join(rootDir, relativePath), 'utf8');
    const match = source.match(/function transitionPresetCommand\(current, event\) \{[\s\S]*?\n        \}/);
    assert.ok(match, `transitionPresetCommand was not found in ${relativePath}`);
    return new Function(`return (${match[0]});`)();
}

function initialState(activePc = -1, activeFreshness = 'unknown') {
    return {
        activePc,
        activeFreshness,
        pending: null,
        lastRequestedPc: -1,
        commandStatus: 'idle'
    };
}

for (const frontend of ['index.html', 'firmware/data/index.html']) {
    describe(`Operator preset state in ${frontend}`, () => {
        const transition = loadTransition(frontend);

        it('does not claim a requested or accepted preset is active', () => {
            let state = initialState(0, 'confirmed');
            state = transition(state, { type: 'request', requestId: 1, pc: 3, bank: 1, slot: 'A' });
            assert.strictEqual(state.activePc, 0);
            assert.strictEqual(state.commandStatus, 'queued');

            state = transition(state, { type: 'accepted', requestId: 1 });
            assert.strictEqual(state.activePc, 0);
            assert.strictEqual(state.commandStatus, 'waiting');
            assert.strictEqual(state.pending.pc, 3);
        });

        it('marks a preset active only after matching pedal confirmation', () => {
            let state = transition(initialState(0, 'confirmed'), {
                type: 'request', requestId: 4, pc: 8, bank: 2, slot: 'C'
            });
            state = transition(state, { type: 'confirmed', pc: 8 });

            assert.strictEqual(state.activePc, 8);
            assert.strictEqual(state.activeFreshness, 'confirmed');
            assert.strictEqual(state.pending, null);
            assert.strictEqual(state.commandStatus, 'confirmed');
        });

        it('keeps the newest rapid selection pending through an older confirmation', () => {
            let state = transition(initialState(0, 'confirmed'), {
                type: 'request', requestId: 10, pc: 1, bank: 0, slot: 'B'
            });
            state = transition(state, {
                type: 'request', requestId: 11, pc: 2, bank: 0, slot: 'C'
            });
            state = transition(state, { type: 'accepted', requestId: 10 });
            state = transition(state, { type: 'confirmed', pc: 1 });

            assert.strictEqual(state.activePc, 1);
            assert.strictEqual(state.pending.requestId, 11);
            assert.strictEqual(state.pending.pc, 2);
            assert.strictEqual(state.commandStatus, 'queued');

            state = transition(state, { type: 'accepted', requestId: 11 });
            state = transition(state, { type: 'confirmed', pc: 2 });
            assert.strictEqual(state.activePc, 2);
            assert.strictEqual(state.pending, null);
            assert.strictEqual(state.commandStatus, 'confirmed');
        });

        it('preserves the last confirmed preset on timeout', () => {
            let state = transition(initialState(6, 'confirmed'), {
                type: 'request', requestId: 20, pc: 9, bank: 3, slot: 'A'
            });
            state = transition(state, { type: 'timeout', requestId: 20 });

            assert.strictEqual(state.activePc, 6);
            assert.strictEqual(state.activeFreshness, 'confirmed');
            assert.strictEqual(state.pending, null);
            assert.strictEqual(state.commandStatus, 'timed_out');
            assert.strictEqual(state.lastRequestedPc, 9);
        });

        it('marks the active preset stale and interrupts pending work on disconnect', () => {
            let state = transition(initialState(12, 'confirmed'), {
                type: 'request', requestId: 30, pc: 13, bank: 4, slot: 'B'
            });
            state = transition(state, { type: 'disconnected' });

            assert.strictEqual(state.activePc, 12);
            assert.strictEqual(state.activeFreshness, 'stale');
            assert.strictEqual(state.pending, null);
            assert.strictEqual(state.commandStatus, 'disconnected');
        });

        it('labels direct WebMIDI sends as unconfirmed', () => {
            let state = transition(initialState(15, 'confirmed'), {
                type: 'request', requestId: 40, pc: 16, bank: 5, slot: 'B'
            });
            state = transition(state, { type: 'local_sent', requestId: 40 });

            assert.strictEqual(state.activePc, 15);
            assert.strictEqual(state.pending, null);
            assert.strictEqual(state.commandStatus, 'unconfirmed');
            assert.strictEqual(state.lastRequestedPc, 16);
        });
    });
}

describe('Preset command bridge contract', () => {
    const frontend = fs.readFileSync(path.join(rootDir, 'index.html'), 'utf8');
    const firmware = fs.readFileSync(path.join(rootDir, 'firmware/src/ws_bridge.cpp'), 'utf8');

    it('correlates browser commands and firmware responses with request IDs', () => {
        assert.match(frontend, /action: 'midi_send'[\s\S]*?request_id: requestId/);
        assert.match(frontend, /data\.event === 'midi_accepted'/);
        assert.match(firmware, /doc\["event"\] = "midi_accepted"/);
        assert.match(firmware, /broadcastError\("midi_invalid"[^;]*requestId\)/);
    });

    it('does not publish active status merely because USB accepted a MIDI command', () => {
        const midiAction = firmware.match(/if \(strcmp\(action, "midi_send"\) == 0\) \{[\s\S]*?\n\s*\} else if/);
        assert.ok(midiAction, 'midi_send action block was not found');
        assert.doesNotMatch(midiAction[0], /broadcastStatus/);
        assert.match(midiAction[0], /broadcastMidiAccepted/);
    });
});
