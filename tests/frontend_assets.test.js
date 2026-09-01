const { describe, it } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const rootDir = path.join(__dirname, '..');

function read(relativePath) {
    return fs.readFileSync(path.join(rootDir, relativePath));
}

describe('Deployed Frontend Assets', () => {
    it('keeps the firmware-hosted application identical to the root application', () => {
        assert.deepStrictEqual(
            read('firmware/data/index.html'),
            read('index.html'),
            'Run the same frontend update against index.html and firmware/data/index.html'
        );
    });

    it('keeps the firmware-hosted favicon identical to the root favicon', () => {
        assert.deepStrictEqual(
            read('firmware/data/favicon.svg'),
            read('favicon.svg'),
            'Run the same favicon update against favicon.svg and firmware/data/favicon.svg'
        );
    });

    it('keeps the firmware-hosted interface theme identical to the root theme', () => {
        assert.deepStrictEqual(
            read('firmware/data/ui.css'),
            read('ui.css'),
            'Run the same theme update against ui.css and firmware/data/ui.css'
        );
    });

    it('keeps the firmware-hosted Wi-Fi setup page identical to the root setup page', () => {
        assert.deepStrictEqual(
            read('firmware/data/setup.html'),
            read('setup.html'),
            'Run the same setup update against setup.html and firmware/data/setup.html'
        );
    });

    it('keeps the firmware-hosted Wi-Fi setup theme identical to the root setup theme', () => {
        assert.deepStrictEqual(
            read('firmware/data/setup.css'),
            read('setup.css'),
            'Run the same setup theme update against setup.css and firmware/data/setup.css'
        );
    });
});
