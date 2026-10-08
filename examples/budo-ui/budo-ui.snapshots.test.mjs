// Draw-command snapshots of the widget gallery: every canvas call of each
// section, rounded, compared with snapshots/<name>.json. A change in how a
// widget draws shows up as a reviewable diff.
//
//   node --test examples/budo-ui/budo-ui.snapshots.test.mjs
//   UPDATE_SNAPSHOTS=1 node --test examples/budo-ui/budo-ui.snapshots.test.mjs   (accept changes)

import assert from 'node:assert/strict';
import test from 'node:test';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const snapshotDir = join(here, 'snapshots');
const update = process.env.UPDATE_SNAPSHOTS === '1';

/** @type {any[][]} */
let calls = [];
const round = value => typeof value === 'number' ? Math.round(value * 10) / 10
    : Array.isArray(value) ? value.map(round)
        : value && typeof value === 'object' ? Object.fromEntries(Object.entries(value).map(([k, v]) => [k, round(v)]))
            : value;
const record = name => (...args) => { calls.push([name, ...args.map(round)]); };
const metrics = (text, width, size, options = {}) => {
    const lineHeight = (options.lineHeight || 1.25) * size;
    const textWidth = String(text).length * size * 0.5;
    const lines = width > 0 ? Math.max(1, Math.ceil(textWidth / width)) : 1;
    const shown = options.maxLines ? Math.min(lines, options.maxLines) : lines;
    return { width: Math.min(textWidth, width || textWidth), height: shown * lineHeight, lines: shown };
};
const spansText = spans => spans.map(span => typeof span === 'string' ? span : span.text).join('');
const canvasNames = ['clear', 'save', 'restore', 'translate', 'scale', 'rotate', 'clipRect', 'clipRoundRect', 'clipPath',
    'saveLayer', 'setFillColor', 'setStrokeColor', 'setStrokeWidth', 'setStrokeCap', 'setAlpha', 'setGradient',
    'setStrokeJoin', 'setImageFilter', 'drawRect', 'drawRoundRect', 'drawCircle', 'drawLine', 'drawArc', 'drawText'];
/** Path data by path id, so drawn icons show up in snapshots by their data. */
const paths = [];
globalThis.sys = {
    canvas: {
        ...Object.fromEntries(canvasNames.map(name => [name, record(name)])),
        drawPath: id => { calls.push(['drawPath', paths[id]]); },
        measureText: (text, size) => String(text).length * size * 0.5,
        measureParagraph: (text, width, size, options) => metrics(text, width, size, options),
        drawParagraph: (text, x, y, width, size, options) => {
            calls.push(['drawParagraph', text, ...[x, y, width, size].map(round), round(options)]);
            return metrics(text, width, size, options);
        },
        measureRichText: (spans, width, options) => metrics(spansText(spans), width, options.size, options),
        drawRichText: (spans, x, y, width, options) => {
            calls.push(['drawRichText', round(spans), ...[x, y, width].map(round), round(options)]);
            return metrics(spansText(spans), width, options.size, options);
        },
    },
    path: { create: () => paths.push('') - 1, addSvg: (id, d) => { paths[id] += d; return true; } },
    input: { isKeyPressed: () => false, startTextInput: () => { }, updateTextInput: () => { }, stopTextInput: () => { } },
    window: { getWidth: () => 900, getHeight: () => 640, getDisplayDensity: () => 1 },
    animation: { requestFrame: () => { } },
};

const { createUI, themes } = await import('./budo-ui.js');
const { createGalleryState, drawGallery, SECTIONS } = await import('./gallery.js');

/** Draw `frames` frames of the gallery and return the last frame's calls. */
function render(section, setup = () => { }, frames = 3, theme = themes.light, clicks = []) {
    const ui = createUI();
    ui.setTheme(theme);
    ui.reducedMotion = true;
    const state = createGalleryState();
    state.section = section;
    setup(state);
    const rect = { x: 16, y: 16, width: 868, height: 608 };
    const frame = (pointer) => {
        calls = [];
        ui.begin({ pointer, keyboard: {}, mouse: {}, totalTime: 1, deltaTime: 1 / 60, text: '', textEdit: null,
            composition: { active: false, changed: false, text: '', selectionEnd: 0 } });
        ui.clear();
        drawGallery(ui, rect, state);
        ui.end();
    };
    const idle = { id: 1, x: 2, y: 2, pressed: false, down: false, type: 'mouse' };
    for (const [x, y] of clicks) {
        frame({ ...idle, x, y });
        frame({ ...idle, x, y, pressed: true, down: true });
        frame({ ...idle, x, y });
    }
    for (let i = 0; i < frames; i++) frame(idle);
    return calls;
}

function compare(name, actual) {
    const file = join(snapshotDir, name + '.json');
    const text = JSON.stringify(actual, null, 0).replace(/\],\[/g, '],\n[') + '\n';
    if (update || !existsSync(file)) {
        mkdirSync(snapshotDir, { recursive: true });
        writeFileSync(file, text);
        return;
    }
    const expected = JSON.parse(readFileSync(file, 'utf8'));
    const index = expected.findIndex((call, i) => JSON.stringify(call) !== JSON.stringify(actual[i]));
    if (index >= 0 || expected.length !== actual.length) {
        const at = index >= 0 ? index : Math.min(expected.length, actual.length);
        assert.fail(`${name}: draw call ${at} differs (UPDATE_SNAPSHOTS=1 accepts the change)\n` +
            `  expected: ${JSON.stringify(expected[at])}\n  actual:   ${JSON.stringify(actual[at])}`);
    }
}

for (const section of SECTIONS)
    test(`gallery snapshot: ${section}`, () => compare(section.toLowerCase(), render(section)));

test('gallery snapshot: choices in the dark theme', () => compare('choices-dark', render('Choices', undefined, 3, themes.dark)));

test('gallery snapshot: an open select menu', () => {
    // The color select sits in the Choices section; click it to open its menu.
    const calls = render('Choices', undefined, 3, themes.light, [[300, 505]]);
    assert.ok(calls.some(call => call[0] === 'drawText' && call[1] === 'Amber'), 'menu options are drawn');
    compare('choices-select-open', calls);
});

test('gallery snapshot: dialog and sheet', () => {
    compare('overlays-dialog', render('Overlays', state => { state.dialogOpen = true; }));
    compare('overlays-sheet', render('Overlays', state => { state.sheetOpen = true; }));
});

test('every section draws only finite numbers', () => {
    for (const section of SECTIONS)
        for (const call of render(section))
            for (const value of call.slice(1))
                if (typeof value === 'number') assert.ok(Number.isFinite(value), `${section}: ${JSON.stringify(call)}`);
});
