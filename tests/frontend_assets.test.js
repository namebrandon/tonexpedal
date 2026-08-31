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
});
