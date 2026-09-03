const { describe, it } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const { validateWifiSettings } = require('../tools/bridge_simulator');

const rootDir = path.join(__dirname, '..');
const setupSource = fs.readFileSync(path.join(rootDir, 'setup.html'), 'utf8');
const firmwareSource = fs.readFileSync(path.join(rootDir, 'firmware/src/main.cpp'), 'utf8');
const partitionTable = fs.readFileSync(path.join(rootDir, 'firmware/partitions_16MB.csv'), 'utf8');

function loadBrowserValidator() {
    const byteMatch = setupSource.match(/function byteLength\(value\) \{[\s\S]*?\n        \}/);
    const validationMatch = setupSource.match(/function validateWifiInput\(settings\) \{[\s\S]*?\n        \}/);
    assert.ok(byteMatch && validationMatch, 'Wi-Fi browser validators were not found');
    const byteLength = new Function(`return (${byteMatch[0]});`)();
    return new Function('byteLength', `return (${validationMatch[0]});`)(byteLength);
}

function validSettings(overrides = {}) {
    return {
        ssid: 'Studio-WLAN',
        password: 'correct-horse',
        hostname: 'tonex-stage',
        open_network: false,
        ...overrides
    };
}

describe('Wi-Fi setup validation', () => {
    const browserValidator = loadBrowserValidator();

    it('keeps browser and simulator validation aligned for valid settings', () => {
        const settings = validSettings();
        assert.strictEqual(browserValidator(settings), null);
        assert.strictEqual(validateWifiSettings(settings), null);
    });

    it('measures SSID limits in UTF-8 bytes', () => {
        const settings = validSettings({ ssid: 'é'.repeat(17) });
        assert.strictEqual(browserValidator(settings).field, 'ssid');
        assert.strictEqual(validateWifiSettings(settings).field, 'ssid');
    });

    it('requires an explicit open-network choice for an empty password', () => {
        const accidental = validSettings({ password: '' });
        assert.strictEqual(browserValidator(accidental).field, 'password');
        assert.strictEqual(validateWifiSettings(accidental).field, 'password');

        const intentional = validSettings({ password: '', open_network: true });
        assert.strictEqual(browserValidator(intentional), null);
        assert.strictEqual(validateWifiSettings(intentional), null);
    });

    it('measures password limits in UTF-8 bytes', () => {
        const settings = validSettings({ password: 'é'.repeat(32) });
        assert.strictEqual(browserValidator(settings).field, 'password');
        assert.strictEqual(validateWifiSettings(settings).field, 'password');
    });

    it('rejects hostnames that cannot safely form an mDNS name', () => {
        for (const hostname of ['-tonex', 'tonex-', 'tone_x', 'tone x']) {
            const settings = validSettings({ hostname });
            assert.strictEqual(browserValidator(settings).field, 'hostname');
            assert.strictEqual(validateWifiSettings(settings).field, 'hostname');
        }
    });
});

describe('Wi-Fi setup firmware contract', () => {
    it('accepts writes only while setup mode is active', () => {
        assert.match(firmwareSource, /if \(!wifiSetupMode\)[\s\S]*?"setup_required"/);
        assert.match(firmwareSource, /wifiHandler->setMethod\(HTTP_POST\)/);
        assert.match(firmwareSource, /const uint64_t requestId/);
    });

    it('stores credentials in NVS but never adds the password to API documents', () => {
        assert.match(firmwareSource, /preferences\.putString\("pass", settings\.password\.c_str\(\)\)/);
        assert.doesNotMatch(firmwareSource, /doc\["password"\]/);
        assert.doesNotMatch(firmwareSource, /accepted\["password"\]/);
        assert.doesNotMatch(setupSource, /localStorage/);
    });

    it('uses a temporary setup AP while retaining station mode as normal operation', () => {
        assert.match(firmwareSource, /WiFi\.mode\(WIFI_AP_STA\)/);
        assert.match(firmwareSource, /WiFi\.mode\(WIFI_STA\)/);
        assert.match(firmwareSource, /stopSetupAccessPoint\(\)/);
    });

    it('mounts the LittleFS partition using the label in the production partition table', () => {
        assert.match(partitionTable, /^littlefs,\s*data,\s*spiffs,/m);
        assert.match(firmwareSource, /LittleFS\.begin\(true, "\/littlefs", 10, "littlefs"\)/);
    });
});
