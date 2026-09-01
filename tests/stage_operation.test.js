const { describe, it } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const rootDir = path.join(__dirname, '..');

function loadShortcut(relativePath) {
    const source = fs.readFileSync(path.join(rootDir, relativePath), 'utf8');
    const match = source.match(/function stageShortcutIndex\(key\) \{[\s\S]*?\n        \}/);
    assert.ok(match, `stageShortcutIndex was not found in ${relativePath}`);
    return new Function(`return (${match[0]});`)();
}

for (const frontend of ['index.html', 'firmware/data/index.html']) {
    describe(`Stage operation in ${frontend}`, () => {
        const source = fs.readFileSync(path.join(rootDir, frontend), 'utf8');
        const shortcut = loadShortcut(frontend);

        it('maps only number keys 1 through 9 to performance pads', () => {
            assert.strictEqual(shortcut('1'), 0);
            assert.strictEqual(shortcut('9'), 8);
            for (const key of ['0', '10', 'a', 'Enter', '']) assert.strictEqual(shortcut(key), -1);
        });

        it('uses semantic keyboard-focusable controls', () => {
            assert.match(source, /document\.createElement\('button'\)/);
            assert.match(source, /row\.setAttribute\('role', 'button'\)/);
            assert.match(source, /e\.key !== 'Enter' && e\.key !== ' '/);
        });

        it('blocks editing and drag/drop while stage lock is active', () => {
            assert.match(source, /function handleDrop[\s\S]*?if \(stageModeActive\) return/);
            assert.match(source, /function openEditModal[\s\S]*?if \(stageModeActive\) return/);
            assert.match(source, /btn\.draggable = !stageModeActive/);
            assert.match(source, /row\.draggable = !stageModeActive/);
        });

        it('supports fullscreen and reacquires a screen wake lock after visibility changes', () => {
            assert.match(source, /navigator\.wakeLock\.request\('screen'\)/);
            assert.match(source, /document\.documentElement\.requestFullscreen\(\)/);
            assert.match(source, /!document\.hidden && stageModeActive && !stageWakeLock/);
        });
    });
}

describe('Stage operation styling', () => {
    const stylesheet = fs.readFileSync(path.join(rootDir, 'ui.css'), 'utf8');

    it('presents touch-safe pads and hides editing surfaces in stage mode', () => {
        assert.match(stylesheet, /body\.stage-mode \.library-panel/);
        assert.match(stylesheet, /body\.stage-mode \.grid-btn[\s\S]*?touch-action: manipulation/);
        assert.match(stylesheet, /grid-template-rows: repeat\(3, minmax\(0, 1fr\)\)/);
    });
});
