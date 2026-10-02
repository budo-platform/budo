import assert from 'node:assert/strict';
import test from 'node:test';
import { createUI } from './ui-library.js';

const noop = () => {};
const sessions = [];
const updates = [];
globalThis.sys = {
    canvas: {
        save: noop, restore: noop, clipRect: noop, setFillColor: noop,
        setStrokeColor: noop, setStrokeWidth: noop, drawRoundRect: noop,
        drawText: noop, drawLine: noop, measureText: text => text.length * 8,
    },
    input: {
        startTextInput: options => sessions.push(options),
        stopTextInput: noop, updateTextInput: options => updates.push(options),
        isKeyPressed: () => false,
    },
};

const bounds = { x: 0, y: 0, width: 100, height: 100 };
const pointer = { x: -1, y: -1, type: 'mouse', pressed: false, down: false };

test('transcript follows new output until the reader scrolls up', () => {
    const ui = createUI();
    function frame(contentHeight, wheelY = 0) {
        ui.begin({ pointer: { ...pointer, x: 20, y: 20 }, mouse: { wheelY } }, bounds);
        const offset = ui.scroll('transcript', bounds, contentHeight, noop, { followEnd: true });
        ui.end();
        return offset;
    }

    assert.equal(frame(200), 100);
    assert.equal(frame(200, 1), 62);
    assert.equal(frame(240), 62);
    assert.equal(frame(240, -10), 140);
    assert.equal(frame(260), 160);
});

test('prompt focus ignores stale edits and disabled actions cannot fire', () => {
    const ui = createUI();
    const model = { value: 'Hello' };
    ui.begin({ pointer, text: '', textEdit: { text: 'old', selectionStart: 0, selectionEnd: 0 },
        totalTime: 1, composition: { active: false, changed: false, text: '', selectionEnd: 0 } }, bounds);
    ui.focusField('prompt', model);
    ui.field('prompt', { x: 0, y: 0, width: 80, height: 48 }, model);
    assert.equal(ui.button('send', 'Send', { x: 0, y: 50, width: 80, height: 40 },
        { enabled: false }), false);
    ui.end();

    assert.equal(model.value, 'Hello');
    assert.equal(sessions.at(-1).selectionEnd, 5);
    assert.equal(updates.at(-1).selectionEnd, 5);
});