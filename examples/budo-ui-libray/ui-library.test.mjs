import assert from 'node:assert/strict';
import test from 'node:test';
import { createUI } from './ui-library.js';

const noop = () => { };
const fillColors = [];
const drawnLines = [];
const drawnText = [];
const textInputUpdates = [];
const textInputStarts = [];
const pressedKeys = new Set();
globalThis.sys = {
    canvas: {
        save: noop, restore: noop, clipRect: noop, setFillColor: color => fillColors.push(color),
        setStrokeColor: noop, setStrokeWidth: noop, drawRoundRect: noop,
        drawText: (...args) => drawnText.push(args),
        drawLine: (...line) => drawnLines.push(line), measureText: text => text.length * 8,
    },
    input: {
        stopTextInput: noop, startTextInput: options => textInputStarts.push(options),
        updateTextInput: update => textInputUpdates.push(update),
        isKeyPressed: key => pressedKeys.has(key),
    },
};

const rect = { x: 0, y: 0, width: 200, height: 120 };

function harness(items, options = {}) {
    const ui = createUI();
    function frame(pointer, wheelY = 0) {
        ui.begin({ pointer, mouse: { wheelY } }, rect);
        const result = ui.list('library', rect, items, options);
        ui.end();
        return result;
    }
    return { frame };
}

function pointer(x, y, pressed, down, type = 'mouse') {
    return { id: 1, x, y, pressed, down, type };
}

test('split divider highlights over its hit area and while dragging', () => {
    const ui = createUI();
    function dividerColor(position, pressed = false, down = false, vertical = false) {
        fillColors.length = 0;
        ui.begin({ pointer: pointer(position.x, position.y, pressed, down) }, rect);
        ui.split('divider', rect, noop, noop, { vertical });
        ui.end();
        return fillColors.at(-1);
    }
    assert.equal(dividerColor({ x: 0, y: 0 }), ui.colors.border);
    assert.equal(dividerColor({ x: 84, y: 50 }), ui.colors.accent);
    assert.equal(dividerColor({ x: 100, y: 50 }, true, true), ui.colors.accent);
    assert.equal(dividerColor({ x: 40, y: 50 }, false, true), ui.colors.accent);
    assert.equal(dividerColor({ x: 50, y: 34 }, false, false, true), ui.colors.accent);
});

test('button text is centered by default, including oversized labels', () => {
    const ui = createUI();
    function buttonTextX(label) {
        drawnText.length = 0;
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.button('button:' + label, label, { x: 20, y: 10, width: 80, height: 40 });
        ui.end();
        return drawnText.at(-1)[1];
    }

    assert.equal(buttonTextX('Four'), 44);
    assert.equal(buttonTextX('A very long label'), -8);
});

test('disabled buttons never click and retain their UI id', () => {
    const ui = createUI();
    const buttonRect = { x: 0, y: 0, width: 80, height: 40 };
    function frame(position) {
        ui.begin({ pointer: position }, rect);
        const clicked = ui.button('send', 'Send', buttonRect, { enabled: false });
        ui.end();
        return clicked;
    }
    assert.equal(frame(pointer(20, 20, true, true)), false);
    assert.equal(frame(pointer(20, 20, false, false)), false);
});

test('checkbox toggles on release inside its row and paints a check', () => {
    const ui = createUI();
    const checkboxRect = { x: 10, y: 10, width: 170, height: 48 };
    let checked = false;
    function frame(position) {
        drawnLines.length = 0;
        ui.begin({ pointer: position }, rect);
        checked = ui.checkbox('crt', 'CRT effect', checkboxRect, checked);
        ui.end();
        return drawnLines.length;
    }
    frame(pointer(30, 30, true, true));
    assert.equal(checked, false);
    frame(pointer(30, 30, false, false));
    assert.equal(checked, true);
    assert.equal(frame(pointer(30, 30, false, false)), 2);
    frame(pointer(30, 30, true, true));
    frame(pointer(190, 30, false, false));
    assert.equal(checked, true);
});

test('followEnd tracks new content until the reader scrolls up', () => {
    const ui = createUI();
    const viewport = { x: 0, y: 0, width: 100, height: 100 };
    function frame(contentHeight, wheelY = 0) {
        ui.begin({ pointer: pointer(20, 20, false, false), mouse: { wheelY } }, rect);
        const offset = ui.scroll('transcript', viewport, contentHeight, noop, { followEnd: true });
        ui.end();
        return offset;
    }
    assert.equal(frame(200), 100);
    assert.equal(frame(200, 1), 62);
    assert.equal(frame(240), 62);
    assert.equal(frame(240, -10), 140);
    assert.equal(frame(260), 160);
});

test('group and child rows have independent selection and actions', () => {
    const { frame } = harness([
        { id: 'song', title: 'Song', kind: 'group', action: true },
        { id: 'preset', title: 'Preset', subtitle: '2 commands', depth: 1, action: true },
    ]);
    frame(pointer(30, 75, true, true));
    assert.equal(frame(pointer(30, 75, false, false)).selectedId, 'preset');
    frame(pointer(175, 20, true, true));
    const action = frame(pointer(175, 20, false, false));
    assert.equal(action.actionId, 'song');
    assert.equal(action.selectedId, null);
});

test('touch scrolling cancels row selection', () => {
    const { frame } = harness(Array.from({ length: 8 }, (_, index) => ({
        id: String(index), title: 'Row ' + index,
    })));
    frame(pointer(30, 85, true, true, 'touch'));
    frame(pointer(30, 20, false, true, 'touch'));
    assert.equal(frame(pointer(30, 20, false, false, 'touch')).selectedId, null);
});

test('dragging a handle returns a move only for a sibling row', () => {
    const { frame } = harness([
        { id: 'song-a', title: 'A', kind: 'group' },
        { id: 'song-b', title: 'B', kind: 'group' },
        { id: 'preset', title: 'P', kind: 'item', groupId: 'song-b' },
    ], { reorder: true });
    frame(pointer(175, 20, true, true, 'touch'));
    frame(pointer(175, 75, false, true, 'touch'));
    assert.deepEqual(frame(pointer(175, 75, false, false, 'touch')).move,
        { fromId: 'song-a', toId: 'song-b' });
    frame(pointer(175, 20, true, true));
    frame(pointer(175, 110, false, true));
    assert.equal(frame(pointer(175, 110, false, false)).move, null);
});

test('field keeps the painted caret and native selection synchronized', () => {
    const ui = createUI();
    const model = { value: 'abcdefghij' };
    const fieldRect = { x: 0, y: 0, width: 70, height: 48 };
    function frame(frameInput) {
        drawnLines.length = 0;
        textInputUpdates.length = 0;
        ui.begin({
            pointer: pointer(28, 24, false, false), mouse: { wheelY: 0 }, text: '',
            textEdit: null, composition: { active: false, changed: false, text: '', selectionEnd: 0 },
            totalTime: 0, ...frameInput,
        }, rect);
        ui.field('name', fieldRect, model);
        ui.end();
    }

    frame({ pointer: pointer(28, 24, true, true) });
    assert.equal(textInputUpdates.at(-1).selectionStart, 2);
    assert.equal(drawnLines.at(-1)[0], textInputUpdates.at(-1).caret.x);

    frame({ textEdit: { text: 'abXYZcdefghij', selectionStart: 0, selectionEnd: 13 } });
    assert.equal(model.value, 'abXYZcdefghij');
    assert.equal(textInputUpdates.at(-1).selectionStart, 0);
    assert.equal(textInputUpdates.at(-1).selectionEnd, 13);
    assert.equal(drawnLines.at(-1)[0], textInputUpdates.at(-1).caret.x);
    assert.ok(textInputUpdates.at(-1).caret.x > fieldRect.x + 12);
    assert.ok(textInputUpdates.at(-1).caret.x <= fieldRect.x + fieldRect.width - 10);
});

test('mouse and arrow keys move the insertion caret in an existing field', () => {
    const ui = createUI();
    const model = { value: 'abcdefghij' };
    const fieldRect = { x: 0, y: 0, width: 70, height: 48 };
    function frame(frameInput = {}, key = null) {
        pressedKeys.clear();
        if (key !== null) pressedKeys.add(key);
        drawnLines.length = 0;
        textInputUpdates.length = 0;
        ui.begin({
            pointer: pointer(0, 0, false, false), text: '', textEdit: null,
            composition: { active: false, changed: false, text: '', selectionEnd: 0 },
            totalTime: 0, ...frameInput,
        }, rect);
        ui.field('name', fieldRect, model);
        ui.end();
        const update = textInputUpdates.at(-1);
        assert.equal(drawnLines.at(-1)[0], update.caret.x);
        return update;
    }

    assert.equal(frame({ pointer: pointer(28, 24, true, true) }).selectionEnd, 2);
    assert.equal(frame({
        pointer: pointer(44, 24, true, true),
        textEdit: { text: 'abcdefghij', selectionStart: 2, selectionEnd: 2 }
    }).selectionEnd, 4);
    assert.equal(frame({}, 80).selectionEnd, 3);
    assert.equal(frame({}, 79).selectionEnd, 4);
    assert.equal(frame({ text: 'X' }).selectionEnd, 5);
    assert.equal(model.value, 'abcdXefghij');
    pressedKeys.clear();
});

test('caret movement restarts the visible blink phase', () => {
    const ui = createUI();
    const model = { value: 'hello' };
    function frame(time, position = pointer(0, 0, false, false), key = null) {
        pressedKeys.clear();
        if (key !== null) pressedKeys.add(key);
        drawnLines.length = 0;
        ui.begin({
            pointer: position, text: '', textEdit: null, totalTime: time,
            composition: { active: false, changed: false, text: '', selectionEnd: 0 },
        }, rect);
        ui.field('name', { x: 0, y: 0, width: 120, height: 48 }, model);
        ui.end();
        return drawnLines.length;
    }

    assert.equal(frame(0.1, pointer(44, 24, true, true)), 1);
    assert.equal(frame(0.7), 0);
    assert.equal(frame(0.71, pointer(0, 0, false, false), 79), 1);
    assert.equal(frame(1.3), 0);
    assert.equal(frame(1.31, pointer(20, 24, true, true)), 1);
    assert.equal(frame(1.9), 0);
    pressedKeys.clear();
});

test('focusField places the caret at the end without applying an old edit', () => {
    const ui = createUI();
    const model = { value: 'hello' };
    textInputStarts.length = 0;
    textInputUpdates.length = 0;
    ui.begin({
        pointer: pointer(0, 0, false, false), text: '',
        textEdit: { text: 'old', selectionStart: 0, selectionEnd: 0 },
        totalTime: 1, composition: { active: false, changed: false, text: '', selectionEnd: 0 }
    }, rect);
    ui.focusField('name', model);
    ui.field('name', { x: 0, y: 0, width: 120, height: 48 }, model);
    ui.end();
    assert.equal(model.value, 'hello');
    assert.equal(textInputStarts.at(-1).selectionEnd, 5);
    assert.equal(textInputUpdates.at(-1).selectionEnd, 5);
});

test('inline editor only changes a name on OK or Enter', () => {
    const ui = createUI();
    const heading = { x: 0, y: 0, width: 200, height: 48 };
    let name = 'Original';
    function frame(position, edit = null, key = null) {
        pressedKeys.clear();
        if (key !== null) pressedKeys.add(key);
        ui.begin({ pointer: position, text: '', textEdit: edit, totalTime: 0 }, rect);
        const renamed = ui.inlineEdit('name:one', name, heading, { maxLength: 48 });
        ui.end();
        if (renamed !== null) name = renamed;
        return renamed;
    }

    frame(pointer(20, 20, true, true));
    frame(pointer(20, 20, false, false));
    assert.equal(frame(pointer(0, 60, false, false),
        { text: '  Renamed  ', selectionStart: 11, selectionEnd: 11 }), null);
    assert.equal(name, 'Original');
    frame(pointer(175, 20, true, true));
    assert.equal(frame(pointer(175, 20, false, false)), 'Renamed');
    assert.equal(name, 'Renamed');

    frame(pointer(20, 20, true, true));
    frame(pointer(20, 20, false, false));
    frame(pointer(0, 60, false, false),
        { text: 'Via Enter', selectionStart: 9, selectionEnd: 9 });
    assert.equal(frame(pointer(0, 60, false, false), null, 40), 'Via Enter');
    pressedKeys.clear();
});

test('inline editor cancels or rejects an empty name without changing its item', () => {
    const ui = createUI();
    const heading = { x: 0, y: 0, width: 200, height: 48 };
    function frame(id, value, position, edit = null, key = null) {
        pressedKeys.clear();
        if (key !== null) pressedKeys.add(key);
        ui.begin({ pointer: position, text: '', textEdit: edit, totalTime: 0 }, rect);
        const renamed = ui.inlineEdit(id, value, heading);
        ui.end();
        return renamed;
    }

    frame('one', 'First', pointer(20, 20, true, true));
    frame('one', 'First', pointer(20, 20, false, false));
    frame('one', 'First', pointer(0, 60, false, false),
        { text: '   ', selectionStart: 3, selectionEnd: 3 });
    assert.equal(frame('one', 'First', pointer(0, 60, false, false), null, 40), null);
    assert.equal(frame('one', 'First', pointer(0, 60, false, false), null, 41), null);

    frame('one', 'First', pointer(20, 20, true, true));
    frame('one', 'First', pointer(20, 20, false, false));
    frame('one', 'First', pointer(0, 60, false, false),
        { text: 'Unsaved', selectionStart: 7, selectionEnd: 7 });
    assert.equal(frame('one', 'First', pointer(20, 70, true, true)), null);

    frame('two', 'Second', pointer(20, 20, true, true));
    frame('two', 'Second', pointer(20, 20, false, false));
    assert.equal(textInputStarts.at(-1).text, 'Second');
    pressedKeys.clear();
});
