const { describe, it } = require('node:test');
const assert = require('node:assert');
const { TOTAL_BANKS, SLOTS } = require('./helpers/tonex_protocol');

function parseImportJson(jsonString) {
    const data = JSON.parse(jsonString);
    if (typeof data !== 'object' || data === null || Array.isArray(data)) {
        throw new Error('Invalid format');
    }
    const validPresets = {};
    let count = 0;
    for (const [key, name] of Object.entries(data)) {
        const match = key.match(/^(\d+)_([ABC])$/);
        if (!match) continue;
        if (typeof name !== 'string' || name.length === 0) continue;
        const bank = parseInt(match[1]);
        const slot = match[2];
        if (bank < 0 || bank >= TOTAL_BANKS) continue;
        validPresets[key] = name.trim();
        count++;
    }
    return { count, validPresets };
}

function exportPresetsJson(presetsMap) {
    const exportData = {};
    for (let bank = 0; bank < TOTAL_BANKS; bank++) {
        for (const slot of SLOTS) {
            const key = `${bank}_${slot}`;
            const info = presetsMap[key];
            if (info && info.name) {
                exportData[key] = info.name;
            }
        }
    }
    return JSON.stringify(exportData, null, 2);
}

describe('Preset JSON Import & Export', () => {
    it('correctly parses valid JSON preset dump', () => {
        const json = JSON.stringify({
            "0_A": "Clean Tweed",
            "0_B": "British 800",
            "49_C": "Acoustic Simulator"
        });
        const result = parseImportJson(json);
        assert.strictEqual(result.count, 3);
        assert.strictEqual(result.validPresets['0_A'], 'Clean Tweed');
        assert.strictEqual(result.validPresets['49_C'], 'Acoustic Simulator');
    });

    it('ignores invalid keys or out-of-bounds banks', () => {
        const json = JSON.stringify({
            "0_A": "Valid Preset",
            "50_A": "Out of bounds bank",
            "0_D": "Invalid slot letter",
            "bad_key": "Invalid format",
            "1_B": "" // Empty name
        });
        const result = parseImportJson(json);
        assert.strictEqual(result.count, 1);
        assert.strictEqual(result.validPresets['0_A'], 'Valid Preset');
    });

    it('throws error on non-object JSON payloads', () => {
        assert.throws(() => parseImportJson(JSON.stringify(["array", "not", "object"])));
        assert.throws(() => parseImportJson(JSON.stringify(123)));
        assert.throws(() => parseImportJson("invalid json syntax"));
    });

    it('exports presets in standard formatted JSON', () => {
        const presetsMap = {
            '0_A': { name: 'Plexi Crunch', amp: true, cab: true },
            '1_B': { name: 'Fuzz Lead', amp: true, cab: false }
        };
        const exported = exportPresetsJson(presetsMap);
        const parsed = JSON.parse(exported);
        assert.deepStrictEqual(parsed, {
            '0_A': 'Plexi Crunch',
            '1_B': 'Fuzz Lead'
        });
    });
});
