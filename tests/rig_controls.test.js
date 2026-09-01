const { describe, it } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const rootDir = path.join(__dirname, '..');

for (const frontendPath of ['index.html', 'firmware/data/index.html']) {
    describe(`Rig Controls UI in ${frontendPath}`, () => {
        const frontend = fs.readFileSync(path.join(rootDir, frontendPath), 'utf8');

        it('presents the three documented switching controls and a model-volume dial', () => {
            for (const control of ['gate', 'compressor', 'reverb']) {
                assert.match(frontend, new RegExp(`data-rig-control="${control}"`));
            }
            assert.match(frontend, /rigCabUnavailable/);
            assert.match(frontend, /class="rig-knob-control" id="rig-model-volume-knob"/);
            assert.match(frontend, /id="rig-model-volume" class="rig-knob-input" type="range" min="0" max="100"/);
        });

        it('uses only MIDI CCs documented for TONEX Pedal', () => {
            assert.match(frontend, /gate: \{ cc: 14,/);
            assert.match(frontend, /compressor: \{ cc: 18,/);
            assert.match(frontend, /reverb: \{ cc: 75,/);
            assert.match(frontend, /volume: \{ cc: 103,/);
            assert.doesNotMatch(frontend, /cc: 117/);
            assert.doesNotMatch(frontend, /cc: 122/);
        });

        it('rate-limits volume changes and does not claim a read-back state', () => {
            assert.match(frontend, /RIG_VOLUME_THROTTLE_MS = 50/);
            assert.match(frontend, /function modelVolumePositionToMidi\(position\)/);
            assert.match(frontend, /\* 127 \/ 100/);
            assert.match(frontend, /function formatModelVolume\(position\)/);
            assert.match(frontend, /\(position \/ 10\)\.toFixed\(1\)/);
            assert.match(frontend, /volume\.addEventListener\('dblclick'/);
            assert.match(frontend, /queueRigVolume\(50\);/);
            assert.match(frontend, /function queueRigVolume\(position\)/);
            assert.match(frontend, /Math\.max\(0, RIG_VOLUME_THROTTLE_MS - elapsed\)/);
            assert.match(frontend, /pedal state is not read back/);
        });
    });
}

describe('Rig control bridge contract', () => {
    const frontend = fs.readFileSync(path.join(rootDir, 'index.html'), 'utf8');
    const firmware = fs.readFileSync(path.join(rootDir, 'firmware/src/ws_bridge.cpp'), 'utf8');

    it('uses a separate, request-correlated MIDI CC command', () => {
        assert.match(frontend, /action: 'midi_cc', cc: control, value: value, channel: channel, request_id: requestId/);
        assert.match(frontend, /data\.event === 'midi_cc_accepted'/);
        assert.match(firmware, /strcmp\(action, "midi_cc"\)/);
        assert.match(firmware, /doc\["event"\] = "midi_cc_accepted"/);
    });

    it('limits firmware controls to the Rig Controls whitelist', () => {
        for (const control of [14, 18, 75, 103]) {
            assert.match(firmware, new RegExp(`control == ${control}`));
        }
        assert.match(firmware, /Unsupported MIDI control, value, or channel/);
    });
});
