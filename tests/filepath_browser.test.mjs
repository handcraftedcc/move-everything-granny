import test from 'node:test';
import assert from 'node:assert/strict';

import {
    buildFilepathBrowserState,
    refreshFilepathBrowser,
    moveFilepathBrowserSelection,
    activateFilepathBrowserItem
} from '../src/shared/filepath_browser.mjs';

function makeFs(tree) {
    const dirs = new Set(Object.keys(tree));
    return {
        readdir(path) {
            if (!dirs.has(path)) {
                throw new Error(`missing dir: ${path}`);
            }
            return tree[path].map((entry) => entry.name);
        },
        stat(path) {
            const idx = path.lastIndexOf('/');
            const parent = idx <= 0 ? '/' : path.slice(0, idx);
            const name = path.slice(idx + 1);
            const items = tree[parent] || [];
            const found = items.find((item) => item.name === name);
            if (!found) return [{ mode: 0o100644 }, 1];
            return [{ mode: found.dir ? 0o040755 : 0o100644 }, 0];
        }
    };
}

test('shows up entry outside root and filters by extension', () => {
    const fs = makeFs({
        '/root': [
            { name: 'A', dir: true },
            { name: 'z.wav', dir: false },
            { name: 'note.txt', dir: false }
        ],
        '/root/A': [
            { name: 'inner.wav', dir: false },
            { name: 'inner.aif', dir: false }
        ]
    });

    const state = buildFilepathBrowserState(
        { key: 'sample_path', name: 'Sample Path', root: '/root', filter: '.wav' },
        '/root/A'
    );

    refreshFilepathBrowser(state, fs);

    assert.equal(state.items[0].kind, 'up');
    assert.equal(state.items[0].label, '..');
    assert.equal(state.items[1].kind, 'file');
    assert.equal(state.items[1].label, 'inner.wav');
    assert.equal(state.items.length, 2);
});

test('sorts folders before files with expected labels', () => {
    const fs = makeFs({
        '/root': [
            { name: 'bdir', dir: true },
            { name: 'adir', dir: true },
            { name: 'c.wav', dir: false },
            { name: 'a.wav', dir: false }
        ],
        '/root/adir': [],
        '/root/bdir': []
    });

    const state = buildFilepathBrowserState(
        { key: 'sample_path', name: 'Sample Path', root: '/root', filter: '.wav' },
        '/root'
    );

    refreshFilepathBrowser(state, fs);

    assert.deepEqual(
        state.items.map((item) => item.label),
        ['[adir]', '[bdir]', 'a.wav', 'c.wav']
    );
});

test('missing selected file falls back to root', () => {
    const fs = makeFs({
        '/root': [{ name: 'sub', dir: true }],
        '/root/sub': [{ name: 'other.wav', dir: false }]
    });

    const state = buildFilepathBrowserState(
        { key: 'sample_path', root: '/root', filter: '.wav' },
        '/root/sub/missing.wav'
    );

    refreshFilepathBrowser(state, fs);
    assert.equal(state.currentDir, '/root');
    assert.equal(state.items[0].label, '[sub]');
});

test('cannot navigate above root via ..', () => {
    const fs = makeFs({
        '/root': [{ name: 'sub', dir: true }],
        '/root/sub': []
    });

    const state = buildFilepathBrowserState(
        { key: 'sample_path', root: '/root', filter: '.wav' },
        '/root/sub'
    );

    refreshFilepathBrowser(state, fs);
    assert.equal(state.items[0].label, '..');

    const openResult = activateFilepathBrowserItem(state);
    assert.equal(openResult.action, 'open');
    assert.equal(state.currentDir, '/root');

    refreshFilepathBrowser(state, fs);
    assert.equal(state.items.some((item) => item.label === '..'), false);
});

test('selecting a file returns full path for param set', () => {
    const fs = makeFs({
        '/root': [{ name: 'sample.wav', dir: false }]
    });

    const state = buildFilepathBrowserState(
        { key: 'sample_path', root: '/root', filter: '.wav' },
        '/root'
    );

    refreshFilepathBrowser(state, fs);
    moveFilepathBrowserSelection(state, 0);

    const result = activateFilepathBrowserItem(state);
    assert.equal(result.action, 'select');
    assert.equal(result.key, 'sample_path');
    assert.equal(result.value, '/root/sample.wav');
    assert.equal(result.filename, 'sample.wav');
});
