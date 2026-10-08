import assert from 'node:assert/strict';
import test from 'node:test';
import { createUI, Spring, springs, mixColors, themes } from './budo-ui.js';
import * as legacy from './ui-library.js';

const noop = () => { };
const fillColors = [];
const drawnLines = [];
const drawnText = [];
const textInputUpdates = [];
const textInputStarts = [];
const pressedKeys = new Set();
globalThis.sys = {
    canvas: {
        save: noop, restore: noop, clipRect: noop, translate: noop, scale: noop, setFillColor: color => fillColors.push(color),
        setStrokeColor: noop, setStrokeWidth: noop, drawRoundRect: noop,
        drawText: (...args) => drawnText.push(args),
        drawLine: (...line) => drawnLines.push(line), measureText: text => text.length * 8,
    },
    input: {
        stopTextInput: noop, startTextInput: options => textInputStarts.push(options),
        updateTextInput: update => textInputUpdates.push(update),
        isKeyPressed: key => pressedKeys.has(key),
    },
    window: { getWidth: () => 640, getHeight: () => 480, getDisplayDensity: () => window.density },
};
const window = { density: 1 };

const rect = { x: 0, y: 0, width: 200, height: 120 };

/** These tests check behavior, not motion: animations jump to their targets. */
function reduced(ui) {
    ui.reducedMotion = true;
    return ui;
}

function harness(items, options = {}) {
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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
    const ui = reduced(createUI());
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


// ── Budo UI additions ────────────────────────────────────────────────────

/** One frame of `draw(ui)` with the given pointer and frame time. */
function step(ui, draw, pointerState = pointer(-1, -1, false, false), dt = 1 / 60) {
    ui.begin({ pointer: pointerState, mouse: { wheelY: 0 }, deltaTime: dt, totalTime: 0 }, rect);
    const result = draw(ui);
    ui.end();
    return result;
}

test('ui-library.js still exports the library', () => {
    assert.equal(legacy.createUI, createUI);
});

test('a spring settles on its target the same way at 60 and 144 frames per second', () => {
    // Same elapsed time (0.25 s) at both rates.
    const run = fps => {
        const spring = new Spring(0, springs.default);
        spring.target = 100;
        for (let frame = 0; frame < 0.25 * fps; frame++) spring.step(1 / fps);
        return spring.value;
    };
    assert.ok(Math.abs(run(60) - run(144)) < 1.5, run(60) + ' vs ' + run(144));
    const spring = new Spring(0);
    spring.target = 1;
    for (let i = 0; i < 120; i++) spring.step(1 / 60);
    assert.ok(spring.settled);
    assert.equal(spring.value, 1);
});

test('a retargeted spring keeps its velocity', () => {
    const spring = new Spring(0);
    spring.target = 100;
    for (let i = 0; i < 5; i++) spring.step(1 / 60);
    const velocity = spring.velocity;
    spring.target = 50;
    assert.equal(spring.velocity, velocity);
});

test('color mixing keeps the ends, and fading keeps the hue', () => {
    assert.equal(mixColors('#006F67', '#FFFFFF', 0), '#006F67');
    assert.equal(mixColors('#006F67', '#FFFFFF', 1), '#FFFFFF');
    assert.equal(mixColors('#336699', '#33669900', 0.5), '#33669980');
});

test('widget colors animate, and ui.animating reports it until they settle', () => {
    const ui = createUI();
    const draw = u => u.button('go', 'Go', { x: 0, y: 0, width: 100, height: 40 }, { primary: true });
    step(ui, draw, pointer(150, 150, false, false));
    assert.equal(ui.animating, false);
    fillColors.length = 0;
    step(ui, draw, pointer(10, 10, true, true));
    step(ui, draw, pointer(10, 10, false, true));
    // A button sets the fill color for its background, then for its label.
    const background = () => fillColors.at(-2);
    const midway = background();
    assert.ok(ui.animating);
    assert.notEqual(midway, themes.light.accent);
    assert.notEqual(midway, themes.light.ink);
    for (let i = 0; i < 60; i++) step(ui, draw, pointer(10, 10, false, true));
    assert.equal(ui.animating, false);
    assert.equal(String(background()).toUpperCase(), themes.light.ink.toUpperCase());
});

test('reduced motion makes animations jump to their targets', () => {
    const ui = reduced(createUI());
    const values = [];
    step(ui, u => values.push(u.spring('x', 0)));
    step(ui, u => values.push(u.spring('x', 10)));
    assert.deepEqual(values, [0, 10]);
    assert.equal(ui.animating, false);
});

test('overlapping widgets: only the one drawn last is hovered and clicked', () => {
    const ui = reduced(createUI());
    const box = { x: 0, y: 0, width: 100, height: 40 };
    const draw = u => [u.button('below', 'Below', box), u.button('above', 'Above', box)];
    step(ui, draw, pointer(10, 10, false, false));
    step(ui, draw, pointer(10, 10, true, true));
    assert.deepEqual(step(ui, draw, pointer(10, 10, false, false)), [false, true]);
});

test('the overlay layer is above the main content', () => {
    const ui = reduced(createUI());
    const box = { x: 0, y: 0, width: 100, height: 40 };
    const draw = u => {
        let overlayClicked = false;
        u.overlay(() => { overlayClicked = u.button('overlay', 'Overlay', box); });
        const baseClicked = u.button('base', 'Base', box);
        return () => [baseClicked, overlayClicked];
    };
    step(ui, draw, pointer(10, 10, false, false));
    step(ui, draw, pointer(10, 10, true, true));
    assert.deepEqual(step(ui, draw, pointer(10, 10, false, false))(), [false, true]);
});

test('scopes prefix ids, so repeated components need no unique ids', () => {
    const ui = reduced(createUI());
    assert.doesNotThrow(() => step(ui, u => {
        for (const name of ['a', 'b']) u.scope(name, () => u.button('ok', 'OK', rect));
    }));
    assert.throws(() => step(ui, u => { u.button('ok', 'OK', rect); u.button('ok', 'OK', rect); }), /Duplicate UI id: ok/);
});

test('presence animates in, keeps drawing while it animates out, then stops', () => {
    const ui = createUI();
    const seen = [];
    const draw = visible => u => u.presence('toast', visible, t => seen.push(t));
    step(ui, draw(true));
    assert.ok(seen[0] < 0.2, 'starts hidden: ' + seen[0]);
    for (let i = 0; i < 60; i++) step(ui, draw(true));
    assert.ok(Math.abs(seen.at(-1) - 1) < 0.01);
    seen.length = 0;
    for (let i = 0; i < 60; i++) step(ui, draw(false));
    assert.ok(seen.length > 3 && seen.length < 60, 'drew ' + seen.length + ' exit frames');
    assert.equal(step(ui, draw(false)), false);
});

test('hit testing follows a layer transform', () => {
    const ui = reduced(createUI());
    const box = { x: 0, y: 0, width: 50, height: 20 };
    const draw = u => {
        let clicked = false;
        u.layer(box, { x: 100 }, () => { clicked = u.button('moved', 'Moved', box); });
        return clicked;
    };
    step(ui, draw, pointer(110, 10, false, false));
    step(ui, draw, pointer(110, 10, true, true));
    assert.equal(step(ui, draw, pointer(110, 10, false, false)), true);
    step(ui, draw, pointer(10, 10, true, true));
    assert.equal(step(ui, draw, pointer(10, 10, false, false)), false);
});

test('select opens on click, returns the picked option, and closes', () => {
    const ui = reduced(createUI());
    const trigger = { x: 0, y: 0, width: 200, height: 40 };
    let value = 'Red';
    const draw = u => { value = u.select('color', trigger, ['Red', 'Green', 'Blue'], value); };
    step(ui, draw, pointer(10, 10, false, false));
    step(ui, draw, pointer(10, 10, true, true));
    drawnText.length = 0;
    step(ui, draw, pointer(10, 10, false, false));
    assert.ok(drawnText.some(args => args[0] === 'Blue'), 'menu shows the options');
    // Options start below the trigger: 4 + 4 dp, rows of row - 4 dp.
    const blueY = 48 + 2 * 44 + 10;
    step(ui, draw, pointer(20, blueY, false, false));
    step(ui, draw, pointer(20, blueY, true, true));
    step(ui, draw, pointer(20, blueY, false, false));
    step(ui, draw, pointer(20, blueY, false, false));
    assert.equal(value, 'Blue');
    drawnText.length = 0;
    step(ui, draw, pointer(300, 300, false, false));
    assert.ok(!drawnText.some(args => args[0] === 'Green'), 'menu closed');
});

test('a press outside an open select closes it without picking', () => {
    const ui = reduced(createUI());
    const trigger = { x: 0, y: 0, width: 200, height: 40 };
    let value = 'Red';
    const draw = u => { value = u.select('color', trigger, ['Red', 'Green'], value); };
    step(ui, draw, pointer(10, 10, true, true));
    step(ui, draw, pointer(10, 10, false, false));
    step(ui, draw, pointer(500, 400, true, true));
    step(ui, draw, pointer(500, 400, false, false));
    drawnText.length = 0;
    step(ui, draw, pointer(500, 400, false, false));
    assert.equal(value, 'Red');
    assert.ok(!drawnText.some(args => args[0] === 'Green'));
});

test('list rows glide to their new position after a reorder', () => {
    const ui = createUI();
    let items = [{ id: 'a', title: 'Alpha' }, { id: 'b', title: 'Beta' }];
    const titleY = title => drawnText.filter(args => args[0] === title).at(-1)[2];
    const draw = u => u.list('l', rect, items, { animate: true });
    for (let i = 0; i < 60; i++) step(ui, draw);
    const top = titleY('Alpha'), second = titleY('Beta');
    items = [items[1], items[0]];
    step(ui, draw);
    const moving = titleY('Alpha');
    assert.ok(moving > top && moving < second, 'between ' + top + ' and ' + second + ': ' + moving);
    for (let i = 0; i < 60; i++) step(ui, draw);
    assert.ok(Math.abs(titleY('Alpha') - second) < 0.5);
});

test('while dragging a row, the other rows make room for it', () => {
    const ui = reduced(createUI());
    const items = [{ id: 'a', title: 'Alpha' }, { id: 'b', title: 'Beta' }, { id: 'c', title: 'Gamma' }];
    const titleY = title => drawnText.filter(args => args[0] === title).at(-1)[2];
    const draw = u => u.list('l', { x: 0, y: 0, width: 200, height: 300 }, items, { reorder: true });
    step(ui, draw, pointer(185, 20, false, false));
    step(ui, draw, pointer(185, 20, true, true));
    const betaBefore = titleY('Beta');
    step(ui, draw, pointer(185, 80, false, true));
    step(ui, draw, pointer(185, 80, false, true));
    assert.ok(titleY('Beta') < betaBefore, 'Beta moved up to make room');
    assert.ok(Math.abs(titleY('Alpha') - (80 - 20 + 2 + 52 / 2 + 6)) < 30, 'Alpha follows the pointer');
});

test('sizes follow the display density', () => {
    window.density = 2;
    try {
        const ui = reduced(createUI());
        step(ui, u => assert.equal(u.dp(10), 20));
        drawnText.length = 0;
        step(ui, u => u.label('Hi', rect));
        assert.equal(drawnText.at(-1)[3], 40);
    } finally {
        window.density = 1;
    }
});

test('begin() without bounds uses the window', () => {
    const ui = reduced(createUI());
    ui.begin({ pointer: pointer(0, 0, false, false), mouse: {} });
    assert.deepEqual(ui.bounds, { x: 0, y: 0, width: 640, height: 480 });
    ui.end();
});

/** Run `body` with extra canvas functions that record their calls. */
function withCanvas(extra, body) {
    const calls = [];
    const added = {};
    for (const name of extra) added[name] = (...args) => calls.push([name, ...args]);
    Object.assign(sys.canvas, added);
    try {
        body(calls);
    } finally {
        for (const name of extra) delete sys.canvas[name];
    }
}

test('layers fade as a group when the canvas has saveLayer', () => {
    withCanvas(['saveLayer'], calls => {
        const ui = createUI();
        fillColors.length = 0;
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.layer(rect, { opacity: 0.5 }, () => ui.fill(rect, '#336699'));
        ui.end();
        assert.deepEqual(calls, [['saveLayer', 128]]);
        assert.equal(fillColors.at(-1), '#336699');
    });
    const ui = createUI();
    fillColors.length = 0;
    ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
    ui.layer(rect, { opacity: 0.5 }, () => ui.fill(rect, '#336699'));
    ui.end();
    assert.equal(fillColors.at(-1), '#33669980');
});

test('glass blurs the backdrop and falls back to a plain surface', () => {
    withCanvas(['saveLayer', 'clipRoundRect'], calls => {
        const ui = createUI();
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.glass(rect, { blur: 12, radius: 6 });
        ui.end();
        assert.deepEqual(calls[0], ['clipRoundRect', 0, 0, 200, 120, 6, 6]);
        assert.deepEqual(calls[1], ['saveLayer', 255, 0, 0, 200, 120, 12]);
    });
    const ui = createUI();
    fillColors.length = 0;
    ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
    ui.glass(rect);
    ui.end();
    assert.equal(fillColors.at(-1), ui.colors.surface);
});

test('a layer backdrop blur grows with its opacity', () => {
    withCanvas(['saveLayer', 'clipRoundRect'], calls => {
        const ui = createUI();
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.layer(rect, { opacity: 0.25, backdrop: 20 }, noop);
        ui.end();
        assert.deepEqual(calls.map(call => call[0]), ['clipRoundRect', 'saveLayer', 'saveLayer']);
        assert.equal(calls[1][6], 5);
        assert.equal(calls[2][1], 64);
    });
});

test('scroll areas clip to rounded corners when available', () => {
    withCanvas(['clipRoundRect'], calls => {
        const ui = reduced(createUI());
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.scroll('rounded', rect, 400, noop, { radius: 9 });
        ui.end();
        assert.deepEqual(calls[0], ['clipRoundRect', 0, 0, 200, 120, 9, 9]);
    });
});

test('paragraphs use the runtime layout, or one truncated line without it', () => {
    withCanvas(['drawParagraph', 'measureParagraph'], calls => {
        sys.canvas.drawParagraph = (...args) => (calls.push(['drawParagraph', ...args]), { width: 90, height: 50, lines: 2 });
        sys.canvas.measureParagraph = (...args) => (calls.push(['measureParagraph', ...args]), { width: 90, height: 50, lines: 2 });
        const ui = createUI();
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        assert.equal(ui.paragraph('Some long text', { x: 4, y: 6, width: 100, height: 0 }, { align: 'center', maxLines: 2 }).lines, 2);
        assert.equal(ui.measureParagraph('Some long text', 100).height, 50);
        ui.end();
        assert.deepEqual(calls[0], ['drawParagraph', 'Some long text', 4, 6, 100, 20, { align: 'center', lineHeight: 1.25, maxLines: 2 }]);
        assert.deepEqual(calls[1], ['measureParagraph', 'Some long text', 100, 20, { align: 'left', lineHeight: 1.25, maxLines: 0 }]);
    });
    const ui = createUI();
    drawnText.length = 0;
    ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
    const metrics = ui.paragraph('A sentence that does not fit', { x: 0, y: 0, width: 100, height: 0 });
    ui.end();
    assert.equal(metrics.lines, 1);
    assert.ok(drawnText.at(-1)[0].endsWith('...'));
});

test('skeletons shimmer with a gradient and keep the UI animating', () => {
    withCanvas(['setGradient'], calls => {
        const ui = createUI();
        ui.begin({ pointer: pointer(0, 0, false, false), totalTime: 0.7 }, rect);
        ui.skeleton({ x: 0, y: 0, width: 80, height: 16 });
        ui.end();
        assert.equal(calls[0][1], 'linear');
        assert.equal(calls.at(-1)[1], null);
        assert.equal(ui.animating, true);
    });
    const ui = reduced(createUI());
    ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
    ui.skeleton({ x: 0, y: 0, width: 80, height: 16 });
    ui.end();
    assert.equal(ui.animating, false);
});

/** Run `body` with mocked platform services (device, accessibility, animation). */
function withPlatform(overrides, body) {
    const calls = [];
    const record = name => (...args) => { calls.push([name, ...args]); return true; };
    const saved = { device: sys.device, accessibility: sys.accessibility, animation: sys.animation };
    const preferences = { darkMode: false, reducedMotion: false, highContrast: false, fontScale: 1,
        safeArea: { top: 0, right: 0, bottom: 0, left: 0 }, ...overrides.preferences };
    sys.device = {
        getPreferences: () => preferences, setCursor: record('setCursor'), haptic: record('haptic'),
        setClipboardText: record('setClipboardText'), getClipboardText: () => overrides.clipboard ?? null,
    };
    let actions = overrides.actions || [];
    sys.accessibility = {
        isActive: () => true, update: record('update'),
        takeActions: () => { const taken = actions; actions = []; return taken; },
    };
    sys.animation = { requestFrame: record('requestFrame'), waitForInput: record('waitForInput') };
    try {
        body(calls, preferences);
    } finally {
        Object.assign(sys, saved);
    }
}

test('system preferences: reduced motion, dark theme, text scale, and safe areas', () => {
    withPlatform({ preferences: { reducedMotion: true, darkMode: true, fontScale: 1.5,
        safeArea: { top: 30, right: 0, bottom: 20, left: 10 } } }, () => {
        const ui = createUI();
        ui.setTheme('system');
        ui.begin({ pointer: pointer(0, 0, false, false) });
        assert.equal(ui.reducedMotion, true);
        assert.equal(ui.colors.background, themes.dark.background);
        assert.equal(ui.sp(20), 30);
        assert.deepEqual(ui.bounds, { x: 10, y: 30, width: 630, height: 430 });
        ui.end();
        ui.reducedMotion = false; // the app's choice wins
        ui.begin({ pointer: pointer(0, 0, false, false) });
        assert.equal(ui.reducedMotion, false);
        ui.end();
    });
});

test('the hovered widget sets the mouse cursor once', () => {
    withPlatform({}, calls => {
        const ui = reduced(createUI());
        const frame = () => {
            ui.begin({ pointer: pointer(10, 10, false, false), keyboard: {} }, rect);
            ui.field('name', { x: 0, y: 0, width: 100, height: 40 }, { value: '' });
            ui.end();
        };
        frame(); frame(); frame();
        assert.deepEqual(calls.filter(call => call[0] === 'setCursor'), [['setCursor', 'text']]);
    });
});

test('widgets describe themselves to screen readers and obey their actions', () => {
    withPlatform({ actions: [{ id: 'dark', action: 'press', value: '' }] }, calls => {
        const ui = reduced(createUI());
        ui.begin({ pointer: pointer(500, 500, false, false) }, rect);
        const dark = ui.toggle('dark', 'Dark mode', { x: 0, y: 0, width: 200, height: 40 }, false);
        ui.button('save', 'Save', { x: 0, y: 50, width: 100, height: 40 }, { enabled: false });
        ui.label('Hello', { x: 0, y: 100, width: 100, height: 20 });
        ui.end();
        assert.equal(dark, true); // the screen reader pressed it
        assert.ok(calls.some(call => call[0] === 'haptic' && call[1] === 'selection'));
        const nodes = calls.find(call => call[0] === 'update')[1];
        assert.deepEqual(nodes.map(node => [node.role, node.label]),
            [['switch', 'Dark mode'], ['button', 'Save'], ['text', 'Hello']]);
        assert.equal(nodes[0].checked, false);
        assert.equal(nodes[1].disabled, true);
        assert.equal(nodes[1].y, 50);
    });
});

test('sliders step and fields take values from screen readers', () => {
    withPlatform({ actions: [{ id: 'level', action: 'increment', value: '' }, { id: 'name', action: 'setValue', value: 'Ada' }] }, () => {
        const ui = reduced(createUI());
        const model = { value: '' };
        ui.begin({ pointer: pointer(500, 500, false, false), keyboard: {} }, rect);
        const level = ui.slider('level', { x: 0, y: 0, width: 100, height: 30 }, 0.5);
        ui.field('name', { x: 0, y: 40, width: 100, height: 40 }, model);
        ui.end();
        assert.ok(Math.abs(level - 0.6) < 1e-9);
        assert.equal(model.value, 'Ada');
    });
});

test('text fields copy, cut, paste, and select all on desktop', () => {
    withPlatform({ clipboard: 'XY' }, calls => {
        const ui = reduced(createUI());
        const model = { value: 'hello' };
        const frame = (keys, keyboard, extra = {}, focus = false) => {
            pressedKeys.clear();
            for (const key of keys) pressedKeys.add(key);
            ui.begin({ pointer: pointer(500, 500, false, false), keyboard, text: '', textEdit: null,
                composition: { active: false, changed: false, text: '', selectionEnd: 0 }, ...extra }, rect);
            if (focus) ui.focusField('name', model);
            ui.field('name', { x: 0, y: 0, width: 160, height: 40 }, model);
            ui.end();
        };
        frame([], {}, {}, true);
        frame([], {});
        frame([4], { ctrl: true });        // select all
        frame([6], { meta: true });        // copy
        assert.deepEqual(calls.find(call => call[0] === 'setClipboardText'), ['setClipboardText', 'hello']);
        frame([25], { ctrl: true });       // paste over the selection
        assert.equal(model.value, 'XY');
        frame([80], { shift: true });      // extend the selection left by one
        frame([27], { ctrl: true });       // cut it
        assert.equal(model.value, 'X');
        assert.deepEqual(calls.filter(call => call[0] === 'setClipboardText').at(-1), ['setClipboardText', 'Y']);
        frame([25], { ctrl: true }, { nativeTextEditing: true }); // the platform pastes itself
        assert.equal(model.value, 'X');
    });
});

test('nextFrame waits for input when idle and wakes for timers', () => {
    withPlatform({}, calls => {
        const ui = reduced(createUI());
        const frame = () => { };
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.end();
        ui.nextFrame(frame);
        assert.deepEqual(calls.at(-1), ['waitForInput', frame, undefined]);
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.wakeAfter(2);
        ui.end();
        ui.nextFrame(frame);
        assert.deepEqual(calls.at(-1), ['waitForInput', frame, 2000]);
        const moving = createUI();
        moving.begin({ pointer: pointer(0, 0, false, false), deltaTime: 1 / 60 }, rect);
        moving.toggle('t', 'T', rect, false);
        moving.end();
        moving.begin({ pointer: pointer(10, 10, true, true), deltaTime: 1 / 60 }, rect);
        moving.toggle('t', 'T', rect, false);
        moving.end();
        moving.nextFrame(frame);
        assert.equal(calls.at(-1)[0], 'requestFrame');
    });
});

/** One frame with these keys pressed. */
function keyFrame(ui, draw, keys = [], keyboard = {}, pointerState = pointer(900, 900, false, false)) {
    pressedKeys.clear();
    for (const key of keys) pressedKeys.add(key);
    ui.begin({ pointer: pointerState, keyboard, text: '', textEdit: null,
        composition: { active: false, changed: false, text: '', selectionEnd: 0 } }, { x: 0, y: 0, width: 400, height: 300 });
    const result = draw();
    ui.end();
    pressedKeys.clear();
    return result;
}
const TAB = 43, ENTER = 40, SPACE = 44, DOWN = 81, RIGHT = 79, END = 77;

test('Tab and Shift+Tab move the keyboard focus in draw order; Enter clicks', () => {
    const ui = reduced(createUI());
    const buttons = () => ['a', 'b', 'c'].map((id, i) => ui.button(id, id, { x: 0, y: i * 50, width: 100, height: 40 }));
    keyFrame(ui, buttons);
    assert.equal(ui.keyFocus, null);
    keyFrame(ui, buttons, [TAB]);
    assert.equal(ui.keyFocus, 'a');
    keyFrame(ui, buttons, [TAB]);
    keyFrame(ui, buttons, [TAB]);
    assert.equal(ui.keyFocus, 'c');
    keyFrame(ui, buttons, [TAB]);
    assert.equal(ui.keyFocus, 'a'); // wraps
    keyFrame(ui, buttons, [TAB], { shift: true });
    assert.equal(ui.keyFocus, 'c');
    assert.deepEqual(keyFrame(ui, buttons, [ENTER]), [false, false, true]);
    keyFrame(ui, buttons, [DOWN]);
    assert.equal(ui.keyFocus, 'a');
});

test('clicking focuses without the ring; keyboard use shows it', () => {
    const ui = reduced(createUI());
    let value = false;
    const draw = () => { value = ui.toggle('t', 'T', { x: 0, y: 0, width: 200, height: 40 }, value); };
    keyFrame(ui, draw, [], {}, pointer(10, 10, true, true));
    keyFrame(ui, draw, [], {}, pointer(10, 10, false, false));
    assert.equal(ui.keyFocus, 't');
    assert.equal(value, true);
    const strokes = [];
    const stroke = sys.canvas.setStrokeColor;
    sys.canvas.setStrokeColor = color => strokes.push(color);
    try {
        keyFrame(ui, draw);
        assert.equal(strokes.length, 0); // no ring after a click
        keyFrame(ui, draw, [SPACE]);
        assert.equal(value, false);      // Space toggles the focused widget
        keyFrame(ui, draw, [TAB]);
        keyFrame(ui, draw);
        assert.ok(strokes.length > 0);   // ring after keyboard navigation
    } finally {
        sys.canvas.setStrokeColor = stroke;
    }
});

test('tabbing into a field starts editing; tabbing out stops it', () => {
    const ui = reduced(createUI());
    const model = { value: 'abc' };
    const draw = () => {
        ui.button('before', 'Before', { x: 0, y: 0, width: 100, height: 40 });
        ui.field('name', { x: 0, y: 50, width: 200, height: 40 }, model);
        ui.button('after', 'After', { x: 0, y: 100, width: 100, height: 40 });
    };
    keyFrame(ui, draw, [TAB]);
    textInputStarts.length = 0;
    keyFrame(ui, draw, [TAB]);
    keyFrame(ui, draw);
    assert.equal(ui.keyFocus, 'name');
    assert.equal(ui.focus, 'name');
    assert.equal(textInputStarts.at(-1).selectionStart, 3);
    keyFrame(ui, draw, [TAB]);
    keyFrame(ui, draw);
    assert.equal(ui.keyFocus, 'after');
    assert.equal(ui.focus, null);
});

test('focused sliders follow the arrow keys and Home/End', () => {
    const ui = reduced(createUI());
    let value = 0.5;
    const draw = () => { value = ui.slider('s', { x: 0, y: 0, width: 200, height: 30 }, value, { label: 'S' }); };
    keyFrame(ui, draw, [TAB]);
    keyFrame(ui, draw, [RIGHT]);
    assert.ok(Math.abs(value - 0.55) < 1e-9);
    keyFrame(ui, draw, [END]);
    assert.equal(value, 1);
});

test('keyboard focus scrolls hidden widgets into view', () => {
    const ui = reduced(createUI());
    const viewport = { x: 0, y: 0, width: 200, height: 100 };
    let offset = 0;
    const draw = () => {
        offset = ui.scroll('list', viewport, 400, content => {
            for (let i = 0; i < 8; i++) ui.button('row' + i, 'Row ' + i, { x: 0, y: content.y + i * 50, width: 180, height: 40 });
        });
    };
    keyFrame(ui, draw);
    for (let i = 0; i < 4; i++) keyFrame(ui, draw, [TAB]);
    keyFrame(ui, draw);
    assert.equal(ui.keyFocus, 'row3');
    assert.ok(offset >= 3 * 50 + 40 - 100 + 12 - 0.5, 'offset ' + offset);
});

test('a select opens from the keyboard, moves focus into its menu, and picks with Enter', () => {
    const ui = reduced(createUI());
    let value = 'Red';
    const draw = () => { value = ui.select('color', { x: 0, y: 0, width: 200, height: 40 }, ['Red', 'Green', 'Blue'], value); };
    keyFrame(ui, draw, [TAB]);
    keyFrame(ui, draw, [ENTER]);           // opens
    keyFrame(ui, draw);
    assert.equal(ui.keyFocus, 'color:option:0');
    keyFrame(ui, draw, [DOWN]);
    assert.equal(ui.keyFocus, 'color:option:1');
    keyFrame(ui, draw, [ENTER]);           // picks Green
    keyFrame(ui, draw);
    assert.equal(value, 'Green');
    assert.equal(ui.keyFocus, 'color');
});

/** A frame at time `time` with a pointer. */
function at(ui, time, pointerState, draw, extra = {}) {
    pressedKeys.clear();
    for (const key of extra.keys || []) pressedKeys.add(key);
    ui.begin({ pointer: pointerState, keyboard: extra.keyboard || {}, totalTime: time, deltaTime: 1 / 60,
        mouse: extra.mouse || {}, text: '', textEdit: null,
        composition: { active: false, changed: false, text: '', selectionEnd: 0 } }, { x: 0, y: 0, width: 400, height: 300 });
    const result = draw();
    ui.end();
    pressedKeys.clear();
    return result;
}
const tap = (ui, x, y, draw, time = 0) => {
    at(ui, time, pointer(x, y, false, false), draw);
    at(ui, time, pointer(x, y, true, true), draw);
    return at(ui, time, pointer(x, y, false, false), draw);
};

test('radio, segmented, and tabs return the picked option', () => {
    const ui = reduced(createUI());
    let radio = 'a', segment = 'day', tab = 'one';
    const draw = () => {
        radio = ui.radio('r', { x: 0, y: 0, width: 200, height: 90 }, ['a', 'b', 'c'], radio);
        segment = ui.segmented('s', { x: 0, y: 100, width: 300, height: 36 }, ['day', 'week', 'month'], segment);
        tab = ui.tabs('t', { x: 0, y: 150, width: 300, height: 40 }, ['one', 'two'], tab);
    };
    tap(ui, 20, 45, draw);    // second radio row
    assert.equal(radio, 'b');
    tap(ui, 250, 118, draw);  // third segment
    assert.equal(segment, 'month');
    const second = 8 * 3 + 32 + 10; // past the first tab ("one" is 3 × 8 px + padding)
    tap(ui, second, 170, draw);
    assert.equal(tab, 'two');
});

test('menus and context menus return the picked item', () => {
    const ui = reduced(createUI());
    const items = [{ value: 'copy', label: 'Copy' }, { value: 'delete', label: 'Delete', danger: true }];
    let picked = null, context = null;
    const draw = () => {
        const a = ui.menu('m', { x: 0, y: 0, width: 160, height: 40 }, 'Edit', items);
        if (a) picked = a;
        const b = ui.contextMenu('area', { x: 0, y: 150, width: 400, height: 150 }, items);
        if (b) context = b;
    };
    tap(ui, 20, 20, draw);          // open
    tap(ui, 20, 44 + 4 + 42 + 10, draw); // second item: 4 dp padding, rows of 42
    at(ui, 0, pointer(500, 500, false, false), draw);
    assert.equal(picked, 'delete');
    at(ui, 0, pointer(100, 200, false, false), draw, { mouse: { rightPressed: true } });
    tap(ui, 110, 200 + 4 + 4 + 10, draw);
    at(ui, 0, pointer(500, 500, false, false), draw);
    assert.equal(context, 'copy');
});

test('tooltips appear after a hover delay or with keyboard focus', () => {
    const ui = reduced(createUI());
    drawnText.length = 0;
    const draw = () => {
        ui.button('b', 'B', { x: 0, y: 100, width: 80, height: 40 });
        ui.tooltip('b', { x: 0, y: 100, width: 80, height: 40 }, 'Save the file');
    };
    at(ui, 0, pointer(10, 110, false, false), draw);
    at(ui, 0.3, pointer(10, 110, false, false), draw);
    assert.ok(!drawnText.some(args => args[0] === 'Save the file'));
    at(ui, 0.7, pointer(10, 110, false, false), draw);
    assert.ok(drawnText.some(args => args[0] === 'Save the file'));
    drawnText.length = 0;
    at(ui, 1, pointer(500, 500, false, false), draw);
    assert.ok(!drawnText.some(args => args[0] === 'Save the file'));
    at(ui, 1.1, pointer(500, 500, false, false), draw, { keys: [43] });
    at(ui, 1.2, pointer(500, 500, false, false), draw);
    assert.ok(drawnText.some(args => args[0] === 'Save the file'));
});

test('dialogs trap the keyboard focus, dismiss on Escape or outside, and restore focus', () => {
    const ui = reduced(createUI());
    let open = false, dismissed = 0;
    const draw = () => {
        if (ui.button('open', 'Open', { x: 0, y: 0, width: 80, height: 40 })) open = true;
        if (ui.dialog('d', open, content => {
            ui.button('ok', 'OK', { x: content.x, y: content.y, width: 80, height: 40 });
            ui.button('cancel', 'Cancel', { x: content.x + 90, y: content.y, width: 80, height: 40 });
        }, { title: 'Delete?', width: 300, height: 200 })) { open = false; dismissed++; }
    };
    at(ui, 0, pointer(500, 500, false, false), draw, { keys: [43] });
    assert.equal(ui.keyFocus, 'open');
    at(ui, 0, pointer(500, 500, false, false), draw, { keys: [40] }); // Enter opens
    at(ui, 0, pointer(500, 500, false, false), draw);
    assert.equal(ui.keyFocus, 'ok');
    at(ui, 0, pointer(500, 500, false, false), draw, { keys: [43] });
    at(ui, 0, pointer(500, 500, false, false), draw, { keys: [43] });
    assert.equal(ui.keyFocus, 'ok'); // Tab wraps inside the dialog
    at(ui, 0, pointer(500, 500, false, false), draw, { keys: [41] }); // Escape
    at(ui, 0, pointer(500, 500, false, false), draw);
    assert.equal(dismissed, 1);
    at(ui, 0, pointer(500, 500, false, false), draw);
    at(ui, 0, pointer(500, 500, false, false), draw);
    assert.equal(ui.keyFocus, 'open');
    open = true;
    at(ui, 0, pointer(5, 5, false, false), draw);
    tap(ui, 5, 290, draw);  // the scrim, outside the panel
    at(ui, 0, pointer(5, 290, false, false), draw);
    assert.equal(dismissed, 2);
});

test('toasts stack, keep the app awake while shown, then leave', () => {
    withPlatform({}, calls => {
        const ui = reduced(createUI());
        at(ui, 0, pointer(0, 0, false, false), () => { ui.toast('Saved'); ui.toast('Synced', { kind: 'success' }); });
        drawnText.length = 0;
        at(ui, 0.1, pointer(0, 0, false, false), () => { });
        assert.deepEqual(drawnText.map(args => args[0]).filter(text => text === 'Saved' || text === 'Synced').sort(), ['Saved', 'Synced']);
        const ys = drawnText.filter(args => args[0] === 'Saved' || args[0] === 'Synced').map(args => args[2]);
        assert.notEqual(ys[0], ys[1]);
        ui.nextFrame(() => { });
        assert.equal(calls.at(-1)[0], 'waitForInput');
        assert.ok(Math.abs(calls.at(-1)[2] - 2400) < 1);
        drawnText.length = 0;
        at(ui, 3, pointer(0, 0, false, false), () => { });
        at(ui, 3.1, pointer(0, 0, false, false), () => { });
        assert.ok(!drawnText.some(args => args[0] === 'Saved'));
    });
});

test('progress glides to its value; indeterminate progress and spinners keep animating', () => {
    const ui = createUI();
    at(ui, 0, pointer(0, 0, false, false), () => ui.progress('p', { x: 0, y: 0, width: 200, height: 6 }, 0.2));
    assert.equal(ui.animating, false);
    at(ui, 0.1, pointer(0, 0, false, false), () => ui.progress('p', { x: 0, y: 0, width: 200, height: 6 }, 0.8));
    assert.equal(ui.animating, true);
    at(ui, 0.2, pointer(0, 0, false, false), () => ui.progress('q', { x: 0, y: 0, width: 200, height: 6 }, null));
    assert.equal(ui.animating, true);
    at(ui, 0.3, pointer(0, 0, false, false), () => ui.spinner({ x: 0, y: 0, width: 24, height: 24 }));
    assert.equal(ui.animating, true);
});

const touch = (x, y, pressed, down) => ({ id: 7, x, y, pressed, down, type: 'touch' });

test('flinging a scroll area glides on; overscroll stretches and springs back', () => {
    const ui = createUI();
    const viewport = { x: 0, y: 0, width: 200, height: 200 };
    let offset = 0;
    const frame = p => { offset = at(ui, 0, p, () => ui.scroll('s', viewport, 2000, () => { })); };
    frame(touch(100, 150, true, true));
    for (let y = 140; y >= 60; y -= 20) frame(touch(100, y, false, true)); // quick drag up: 90 px
    const released = offset;
    frame(touch(100, 60, false, false));
    for (let i = 0; i < 40; i++) frame(touch(100, 60, false, false));
    assert.ok(offset > released + 50, `momentum carried it from ${released} to ${offset}`);

    const top = createUI();
    let shown = 0;
    const pull = p => { shown = at(top, 0, p, () => top.scroll('t', viewport, 2000, () => { })); };
    pull(touch(100, 20, true, true));
    for (let y = 40; y <= 140; y += 20) pull(touch(100, y, false, true));
    assert.ok(shown < -10, 'stretched past the top: ' + shown);
    assert.ok(shown > -60, 'with resistance: ' + shown);
    pull(touch(100, 140, false, false));
    for (let i = 0; i < 60; i++) pull(touch(100, 140, false, false));
    assert.ok(Math.abs(shown) < 1, 'sprang back: ' + shown);
});

test('swiping a list row far enough reports its swipe action', () => {
    const ui = reduced(createUI());
    const items = [{ id: 'a', title: 'Alpha', swipe: { label: 'Delete', danger: true } }, { id: 'b', title: 'Beta' }];
    const listRect = { x: 0, y: 0, width: 300, height: 200 };
    let result;
    const frame = p => { result = at(ui, 0, p, () => ui.list('l', listRect, items)); };
    frame(pointer(250, 20, false, false));
    frame(pointer(250, 20, true, true));
    for (let x = 230; x >= 90; x -= 20) frame(pointer(x, 22, false, true));
    frame(pointer(90, 22, false, false));
    assert.equal(result.swipedId, 'a');
    assert.equal(result.selectedId, null); // a swipe is not a click
    // A short swipe springs back without an action.
    frame(pointer(250, 20, true, true));
    frame(pointer(220, 20, false, true));
    frame(pointer(220, 20, false, false));
    assert.equal(result.swipedId, null);
});

test('the navigator animates pushes, keeps the leaving page inert, and settles', () => {
    const ui = createUI();
    const stack = ['list'];
    const drawn = [];
    let clicked = null;
    const draw = (t, p) => at(ui, t, p, () => ui.navigator('nav', { x: 0, y: 0, width: 400, height: 300 }, stack, route => {
        drawn.push(route);
        if (ui.button('go', route, { x: 10, y: 10, width: 100, height: 40 })) clicked = route;
    }));
    draw(0, pointer(500, 500, false, false));
    stack.push('detail');
    drawn.length = 0;
    draw(0.016, pointer(50, 30, false, false));
    assert.deepEqual(drawn, ['list', 'detail']);   // both pages during the transition
    draw(0.032, pointer(50, 30, true, true));
    draw(0.048, pointer(50, 30, false, false));
    assert.notEqual(clicked, 'list');              // the leaving page ignores input
    for (let i = 0; i < 90; i++) draw(0.05 + i / 60, pointer(500, 500, false, false));
    drawn.length = 0;
    draw(2, pointer(500, 500, false, false));
    assert.deepEqual(drawn, ['detail']);
});

test('shared elements morph between pages', () => {
    const ui = createUI();
    const stack = ['grid'];
    const rects = [];
    const draw = t => at(ui, t, pointer(500, 500, false, false), () =>
        ui.navigator('nav', { x: 0, y: 0, width: 400, height: 300 }, stack, route => {
            const rect = route === 'grid' ? { x: 20, y: 20, width: 40, height: 40 } : { x: 0, y: 100, width: 400, height: 200 };
            ui.shared('photo', rect, r => rects.push({ route, ...r }));
        }));
    draw(0);
    draw(0.016);
    stack.push('photo');
    for (let i = 0; i < 12; i++) draw(0.032 + i / 60);
    rects.length = 0;
    draw(0.3);
    assert.equal(rects.length, 1);                 // one copy, drawn by the entering page
    const r = rects[0];
    assert.ok(r.width > 40 && r.width < 400, 'morphing width ' + r.width);
    assert.ok(r.y > 20 && r.y < 100, 'morphing y ' + r.y);
});

test('Escape and an edge drag ask the navigator to go back', () => {
    const ui = reduced(createUI());
    const stack = ['a', 'b'];
    const rect = { x: 0, y: 0, width: 400, height: 300 };
    const nav = (p, keys = []) => at(ui, 0, p, () => ui.navigator('nav', rect, stack, () => { }), { keys });
    nav(pointer(500, 500, false, false));
    assert.equal(nav(pointer(500, 500, false, false), [41]), true);
    nav(pointer(5, 100, true, true));
    let back = false;
    for (let x = 40; x <= 260; x += 40) back = nav(pointer(x, 100, false, true)) || back;
    back = nav(pointer(260, 100, false, false)) || back;
    assert.equal(back, true);
});

test('a click asks for one more frame, so state it changes gets drawn', () => {
    withPlatform({}, calls => {
        const ui = reduced(createUI());
        const draw = () => ui.button('b', 'B', { x: 0, y: 0, width: 100, height: 40 });
        at(ui, 0, pointer(10, 10, false, false), draw);
        at(ui, 0, pointer(10, 10, true, true), draw);
        at(ui, 0, pointer(10, 10, false, false), draw);  // the click
        ui.nextFrame(() => { });
        assert.equal(calls.at(-1)[0], 'requestFrame');
        at(ui, 0, pointer(10, 10, false, false), draw);
        ui.nextFrame(() => { });
        assert.equal(calls.at(-1)[0], 'waitForInput');
    });
});

test('with reduced motion, a navigation still reaches its new page without input', () => {
    withPlatform({ preferences: { reducedMotion: true } }, calls => {
        const ui = createUI();
        const stack = ['a'];
        const drawn = [];
        const draw = () => at(ui, 0, pointer(500, 500, false, false), () =>
            ui.navigator('nav', { x: 0, y: 0, width: 400, height: 300 }, stack, route => drawn.push(route)));
        draw();
        stack.push('b');
        drawn.length = 0;
        draw();
        ui.nextFrame(() => { });
        assert.equal(calls.at(-1)[0], 'requestFrame'); // the transition's first frame asks for the next
        drawn.length = 0;
        draw();
        assert.deepEqual(drawn, ['b']);
    });
});

test('content sizes take their measured length and shrink together when needed', () => {
    const ui = reduced(createUI());
    at(ui, 0, pointer(0, 0, false, false), () => {
        const [a, b, c] = ui.columns({ x: 0, y: 0, width: 300, height: 40 },
            [{ content: ui.buttonWidth('Save') }, { weight: 1 }, { content: 50 }], 10);
        assert.equal(a.width, 4 * 8 + 32);
        assert.equal(c.width, 50);
        assert.equal(b.width, 300 - 20 - 64 - 50);
        const [d, e] = ui.columns({ x: 0, y: 0, width: 100, height: 40 }, [{ content: 150 }, { content: 50 }], 0);
        assert.equal(d.width, 75);
        assert.equal(e.width, 25);
    });
});

/** Run `body` with SVG paths, gradients, and blur on the stub canvas; returns the calls. */
function withVectorCanvas(body) {
    const calls = [];
    const data = [];
    const canvas = sys.canvas;
    const saved = { ...canvas }, savedPath = sys.path;
    sys.path = { create: () => data.push('') - 1, addSvg: (id, d) => d.startsWith('M') && !!(data[id] = d) };
    Object.assign(canvas, {
        drawPath: id => calls.push(['drawPath', data[id]]),
        translate: (x, y) => calls.push(['translate', x, y]),
        scale: (x, y) => calls.push(['scale', x, y]),
        setStrokeColor: color => calls.push(['stroke', color]),
        setGradient: (...args) => calls.push(['gradient', ...args]),
        setImageFilter: (...args) => calls.push(['filter', ...args]),
    });
    try {
        body(calls);
    } finally {
        for (const key of Object.keys(canvas)) if (!(key in saved)) delete canvas[key];
        Object.assign(canvas, saved);
        sys.path = savedPath;
    }
    return calls;
}

test('icons draw built-in, registered, or raw path data, scaled from the 24-unit grid', () => {
    const ui = reduced(createUI());
    const calls = withVectorCanvas(calls => {
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.icon('plus', { x: 10, y: 10, width: 48, height: 48 }, { color: '#FF0000' });
        ui.registerIcon('dot', 'M11 12h2');
        ui.icon('dot', { x: 0, y: 0, width: 24, height: 24 });
        ui.icon('M1 1L2 2', { x: 0, y: 0, width: 12, height: 12 });
        ui.icon('not path data', { x: 0, y: 0, width: 12, height: 12 });
        ui.end();
    });
    assert.deepEqual(calls.filter(call => call[0] === 'drawPath').map(call => call[1]), ['M12 5v14M5 12h14', 'M11 12h2', 'M1 1L2 2']);
    assert.deepEqual(calls.find(call => call[0] === 'scale'), ['scale', 2, 2]);
    assert.deepEqual(calls.find(call => call[0] === 'translate'), ['translate', 10, 10]);
    assert.ok(calls.some(call => call[0] === 'stroke' && call[1] === '#FF0000'));
});

test('icons draw nothing on runtimes without sys.path.addSvg', () => {
    const ui = reduced(createUI());
    ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
    assert.doesNotThrow(() => ui.icon('plus', rect));
    assert.doesNotThrow(() => ui.iconButton('add', 'plus', { x: 0, y: 0, width: 40, height: 40 }, { label: 'Add' }));
    ui.end();
});

test('icon buttons click, describe themselves by label, and buttons can carry an icon', () => {
    const ui = reduced(createUI());
    const button = { x: 0, y: 0, width: 40, height: 40 };
    let clicked = false;
    const calls = withVectorCanvas(() => {
        for (const [pressed, down] of [[false, false], [true, true], [false, false]]) {
            ui.begin({ pointer: pointer(20, 20, pressed, down) }, rect);
            clicked = ui.iconButton('add', 'plus', button, { label: 'Add item' }) || clicked;
            ui.button('new', 'New', { x: 50, y: 0, width: 100, height: 40 }, { icon: 'plus' });
            ui.end();
        }
    });
    assert.equal(clicked, true);
    assert.ok(calls.filter(call => call[0] === 'drawPath').length >= 2);
});

test('shadows follow the elevation tokens; the accent gradient replaces flat accent fills', () => {
    const ui = reduced(createUI({ accentGradient: ['#00FF00', '#0000FF'] }));
    const calls = withVectorCanvas(() => {
        ui.begin({ pointer: pointer(0, 0, false, false) }, rect);
        ui.shadow({ x: 0, y: 0, width: 50, height: 20 }, 3);
        ui.button('go', 'Go', { x: 0, y: 30, width: 80, height: 40 }, { primary: true });
        ui.end();
    });
    assert.deepEqual(calls.find(call => call[0] === 'filter'), ['filter', 'blur', themes.light.elevation[2].blur]);
    const gradient = calls.find(call => call[0] === 'gradient' && call[1] === 'linear');
    assert.ok(gradient, 'primary buttons use the gradient');
    assert.deepEqual(gradient.slice(2, 6), [0, 30, 0, 70], 'top to bottom');
    assert.deepEqual(gradient[6], ['#00FF00', '#0000FF']);
});

/** A text area 200×120 at the origin: inner text box 176 wide (22 characters
 *  of the stub's 8 px), lines 28 px tall from y = 10. */
function areaHarness(initial = '', options = {}) {
    const ui = reduced(createUI());
    const model = { value: initial };
    const down = new Set();
    const savedDown = sys.input.isKeyDown;
    let time = 0;
    function frame({ keys = [], keyboard = {}, text = '', textEdit = null, native = false, at = null, press = false, hold = false } = {}) {
        time += 1 / 60;
        for (const key of keys) pressedKeys.add(key);
        sys.input.isKeyDown = key => down.has(key) || keys.includes(key);
        try {
            const [x, y] = at || [-50, -50];
            ui.begin({ pointer: pointer(x, y, press, press || hold), keyboard, text, textEdit, nativeTextEditing: native,
                totalTime: time, deltaTime: 1 / 60 }, { x: 0, y: 0, width: 400, height: 300 });
            ui.textArea('notes', { x: 0, y: 0, width: 200, height: 120 }, model, options);
            ui.end();
        } finally {
            for (const key of keys) pressedKeys.delete(key);
            sys.input.isKeyDown = savedDown;
        }
        return model.value;
    }
    const click = (x, y, keyboard = {}) => { frame({ at: [x, y] }); frame({ at: [x, y], press: true, keyboard }); frame({ at: [x, y] }); };
    const editor = () => ({ value: model.value });
    return { ui, model, frame, click, down, editor, advance: seconds => { time += seconds; } };
}

const K = { A: 4, C: 6, V: 25, Z: 29, Y: 28, ENTER: 40, BACKSPACE: 42, HOME: 74, END: 77, RIGHT: 79, LEFT: 80, DOWN: 81, UP: 82 };

test('a text area types, breaks lines with Enter, and wraps long lines', () => {
    const area = areaHarness();
    area.click(20, 20);
    area.frame({ text: 'hello' });
    area.frame({ keys: [K.ENTER] });
    area.frame({ text: 'world' });
    assert.equal(area.model.value, 'hello\nworld');
    textInputStarts.length = 0;
    const wrapped = areaHarness('one two three four five six seven');
    drawnText.length = 0;
    wrapped.frame();
    const lines = drawnText.map(call => call[0]);
    assert.ok(lines.includes('one two three four'), JSON.stringify(lines));
    assert.ok(lines.includes('five six seven'), JSON.stringify(lines));
});

test('Up and Down keep the column; they stay in the text area instead of moving the focus', () => {
    const area = areaHarness('abcdefgh\nab\nabcdefgh');
    area.click(12 + 6 * 8, 20);               // after "abcdef" on the first line
    area.frame({ keys: [K.DOWN] });           // the short line: its end
    area.frame({ keys: [K.DOWN] });           // back to column 6
    area.frame({ text: '!' });
    assert.equal(area.model.value, 'abcdefgh\nab\nabcdef!gh');
    assert.ok(area.ui.keyFocus === null || area.ui.focus === 'notes');
});

test('Shift extends the selection; Alt moves by words; double-click selects a word', () => {
    const area = areaHarness('alpha beta gamma');
    area.click(12, 20);                                        // caret at 0
    area.frame({ keys: [K.RIGHT], keyboard: { alt: true } });  // after "alpha"
    area.frame({ keys: [K.RIGHT], keyboard: { alt: true, shift: true } }); // select " beta"
    area.frame({ text: '-' });
    assert.equal(area.model.value, 'alpha- gamma');
    const words = areaHarness('alpha beta gamma');
    const x = 12 + 7.5 * 8;                                    // inside "beta"
    words.click(x, 20);
    words.frame({ at: [x, 20], press: true });                 // second press: a double click
    words.frame({ at: [x, 20] });
    words.frame({ text: 'BETA' });
    assert.equal(words.model.value, 'alpha BETA gamma');
});

test('dragging the mouse selects text', () => {
    const area = areaHarness('alpha beta gamma');
    area.frame({ at: [12, 20] });
    area.frame({ at: [12, 20], press: true });
    area.frame({ at: [12 + 5 * 8, 20], hold: true });
    area.frame({ at: [12 + 5 * 8, 20] });
    area.frame({ keys: [K.BACKSPACE] });
    assert.equal(area.model.value, ' beta gamma');
});

test('undo restores whole typing runs, and redo brings them back', () => {
    const area = areaHarness('');
    area.click(20, 20);
    for (const character of 'abc') area.frame({ text: character });
    area.advance(2);
    area.frame({ text: ' def' });
    area.frame({ keys: [K.Z], keyboard: { ctrl: true } });
    assert.equal(area.model.value, 'abc');
    area.frame({ keys: [K.Z], keyboard: { ctrl: true } });
    assert.equal(area.model.value, '');
    area.frame({ keys: [K.Z], keyboard: { ctrl: true, shift: true } });
    assert.equal(area.model.value, 'abc');
    area.frame({ keys: [K.Y], keyboard: { ctrl: true } });
    assert.equal(area.model.value, 'abc def');
});

test('held keys repeat after a delay', () => {
    const area = areaHarness('abcdefghij');
    area.click(12 + 10 * 8, 20);
    area.frame({ keys: [K.BACKSPACE] });
    area.down.add(K.BACKSPACE);
    for (let i = 0; i < 20; i++) area.frame();   // a third of a second: still the delay
    assert.equal(area.model.value, 'abcdefghi');
    for (let i = 0; i < 18; i++) area.frame();   // then about one per 35 ms
    area.down.delete(K.BACKSPACE);
    area.frame();
    assert.ok(area.model.value.length < 9 && area.model.value.length >= 3, area.model.value);
});

test('native editing (web, Android) owns text changes; the widget keeps navigation', () => {
    const area = areaHarness('one');
    area.click(12 + 3 * 8, 20);
    area.frame({ native: true, keys: [K.BACKSPACE] });    // the platform deletes, not the widget
    assert.equal(area.model.value, 'one');
    area.frame({ native: true, textEdit: { text: 'one\ntwo', selectionStart: 7, selectionEnd: 7 } });
    assert.equal(area.model.value, 'one\ntwo');
    textInputUpdates.length = 0;
    area.frame({ native: true, keys: [K.UP] });
    assert.equal(textInputUpdates.at(-1).selectionStart, 3, 'Up moves to the line above');
});

test('read-only text areas select and copy but do not change', () => {
    const area = areaHarness('fixed', { readOnly: true });
    area.click(20, 20);
    area.frame({ text: 'x' });
    area.frame({ keys: [K.BACKSPACE] });
    area.frame({ keys: [K.ENTER] });
    assert.equal(area.model.value, 'fixed');
});

test('Down inside a focused text area does not move the keyboard focus to the next widget', () => {
    const ui = reduced(createUI());
    const model = { value: 'one\ntwo' };
    const frame = keys => {
        for (const key of keys) pressedKeys.add(key);
        try {
            ui.begin({ pointer: pointer(-50, -50, false, false), keyboard: {}, totalTime: 1 }, { x: 0, y: 0, width: 400, height: 300 });
            ui.textArea('notes', { x: 0, y: 0, width: 200, height: 120 }, model);
            ui.button('save', 'Save', { x: 0, y: 130, width: 80, height: 40 });
            ui.end();
        } finally {
            for (const key of keys) pressedKeys.delete(key);
        }
    };
    frame([]);
    frame([43]);            // Tab: the text area
    frame([]);
    assert.equal(ui.keyFocus, 'notes');
    frame([81]);            // Down
    frame([]);
    assert.equal(ui.keyFocus, 'notes');
});

/** Draw one frame of `draw(ui)` with optional pointer and keys. */
function dataFrame(ui, draw, { at = null, press = false, hold = false, keys = [], time = 1 } = {}) {
    for (const key of keys) pressedKeys.add(key);
    try {
        const [x, y] = at || [-50, -50];
        ui.begin({ pointer: pointer(x, y, press, press || hold), keyboard: {}, totalTime: time }, { x: 0, y: 0, width: 640, height: 480 });
        const result = draw(ui);
        ui.end();
        return result;
    } finally {
        for (const key of keys) pressedKeys.delete(key);
    }
}

const TABLE = { x: 0, y: 0, width: 400, height: 241 };   // header 40, rows 40: five rows fill the view
const people = Array.from({ length: 10000 }, (_, i) => ({ id: 'p' + i, name: 'Person ' + i, age: 20 + (i % 50) }));

function tableOptions(extra = {}) {
    const asked = new Set();
    return {
        asked,
        options: {
            columns: [{ key: 'name', label: 'Name', sortable: true }, { key: 'age', label: 'Age', width: 80, align: 'right' }],
            rowCount: people.length,
            row: index => { asked.add(index); return people[index]; },
            rowId: index => people[index].id,
            ...extra,
        },
    };
}

test('tables draw only their visible rows', () => {
    const ui = reduced(createUI());
    const { asked, options } = tableOptions();
    drawnText.length = 0;
    dataFrame(ui, ui => ui.table('people', TABLE, options));
    assert.ok(asked.size <= 8, 'rows asked: ' + asked.size);
    assert.ok(drawnText.some(call => call[0] === 'Person 0'));
    assert.ok(!drawnText.some(call => call[0] === 'Person 50'));
});

test('tables sort from the header, select rows, and move the selection with the keyboard', () => {
    const ui = reduced(createUI());
    let selectedId = null, sort = null, activated = null;
    const draw = ui => {
        const result = ui.table('people', TABLE, { ...tableOptions().options, selectedId, sort });
        selectedId = result.selectedId;
        if (result.sort) sort = result.sort;
        if (result.activatedId) activated = result.activatedId;
    };
    dataFrame(ui, draw, { at: [40, 20] });
    dataFrame(ui, draw, { at: [40, 20], press: true });
    dataFrame(ui, draw, { at: [40, 20] });
    assert.deepEqual(sort, { key: 'name', descending: false });
    dataFrame(ui, draw, { at: [40, 20], press: true });
    dataFrame(ui, draw, { at: [40, 20] });
    assert.deepEqual(sort, { key: 'name', descending: true });
    dataFrame(ui, draw, { at: [40, 100] });            // the second row (y 81..121)
    dataFrame(ui, draw, { at: [40, 100], press: true });
    dataFrame(ui, draw, { at: [40, 100] });
    assert.equal(selectedId, 'p1');
    dataFrame(ui, draw, { keys: [81] });               // Down
    assert.equal(selectedId, 'p2');
    dataFrame(ui, draw, { keys: [77] });               // End
    assert.equal(selectedId, 'p9999');
    dataFrame(ui, draw, { keys: [40] });               // Enter
    assert.equal(activated, 'p9999');
});

test('dragging a header edge resizes the column', () => {
    const ui = reduced(createUI());
    const { options } = tableOptions();
    const draw = ui => ui.table('people', TABLE, options);
    const firstEdge = 1 + (400 - 2 - 12 - 80);        // inner x + (inner width - gutter - the Age column)
    dataFrame(ui, draw, { at: [firstEdge, 20] });
    dataFrame(ui, draw, { at: [firstEdge, 20], press: true });
    dataFrame(ui, draw, { at: [firstEdge - 100, 20], hold: true });
    dataFrame(ui, draw, { at: [firstEdge - 100, 20] });
    drawnText.length = 0;
    dataFrame(ui, draw);
    const age = drawnText.find(call => call[0] === 'Age');
    assert.ok(age && age[1] < firstEdge - 50, 'the Age header moved left: ' + JSON.stringify(age));
});

test('trees expand from the chevron and the arrow keys, and report toggles', () => {
    const ui = reduced(createUI());
    const nodes = [
        { id: 'src', label: 'src', children: [{ id: 'main', label: 'main.js' }, { id: 'lib', label: 'lib', children: [{ id: 'core', label: 'core.js' }] }] },
        { id: 'readme', label: 'README.md' },
    ];
    let selectedId = null, toggled = [];
    const draw = ui => {
        const result = ui.tree('files', { x: 0, y: 0, width: 300, height: 300 }, nodes, { selectedId });
        selectedId = result.selectedId;
        if (result.toggledId) toggled.push(result.toggledId);
    };
    drawnText.length = 0;
    dataFrame(ui, draw);
    assert.ok(!drawnText.some(call => call[0] === 'main.js'), 'collapsed at first');
    dataFrame(ui, draw, { at: [20, 22] });                     // the chevron of "src"
    dataFrame(ui, draw, { at: [20, 22], press: true });
    dataFrame(ui, draw, { at: [20, 22] });
    drawnText.length = 0;
    dataFrame(ui, draw);
    assert.ok(drawnText.some(call => call[0] === 'main.js'), 'expanded');
    assert.deepEqual(toggled, ['src']);
    dataFrame(ui, draw, { at: [120, 22] });                    // select "src" by its label
    dataFrame(ui, draw, { at: [120, 22], press: true });
    dataFrame(ui, draw, { at: [120, 22] });
    assert.equal(selectedId, 'src');
    dataFrame(ui, draw, { keys: [81] });                       // Down: main.js
    dataFrame(ui, draw, { keys: [81] });                       // Down: lib
    assert.equal(selectedId, 'lib');
    dataFrame(ui, draw, { keys: [79] });                       // Right: expand lib
    dataFrame(ui, draw, { keys: [79] });                       // Right: into core.js
    assert.equal(selectedId, 'core');
    dataFrame(ui, draw, { keys: [80] });                       // Left: up to lib
    assert.equal(selectedId, 'lib');
    dataFrame(ui, draw, { keys: [80] });                       // Left: collapse lib
    assert.deepEqual(toggled, ['src', 'lib', 'lib']);
});

/** A field frame with text-session state the tests can read back. */
function fieldInput(position, extra = {}) {
    return {
        pointer: position, mouse: { wheelY: 0 }, text: '', textEdit: null,
        composition: { active: false, changed: false, text: '', selectionEnd: 0 }, totalTime: 0, ...extra,
    };
}

test('a touch opens the keyboard when the tap lifts, not when the finger lands', () => {
    const ui = reduced(createUI());
    const model = { value: 'hello' };
    const frame = position => {
        ui.begin(fieldInput(position), rect);
        ui.field('name', { x: 0, y: 0, width: 160, height: 48 }, model);
        ui.end();
    };
    textInputStarts.length = 0;
    frame(pointer(40, 24, true, true, 'touch'));
    assert.equal(textInputStarts.length, 0, 'no keyboard on touch down');
    assert.equal(ui.focus, null);
    frame(pointer(40, 24, false, false, 'touch'));
    assert.equal(textInputStarts.length, 1, 'the tap opens the keyboard');
    assert.equal(ui.focus, 'name');
});

test('a mouse press still focuses a field and places the caret at once', () => {
    const ui = reduced(createUI());
    textInputStarts.length = 0;
    ui.begin(fieldInput(pointer(40, 24, true, true)), rect);
    ui.field('name', { x: 0, y: 0, width: 160, height: 48 }, { value: 'hello' });
    ui.end();
    assert.equal(textInputStarts.length, 1);
    assert.equal(ui.focus, 'name');
});

test('a scroll that starts on a field scrolls and never opens the keyboard', () => {
    const ui = reduced(createUI());
    const viewport = { x: 0, y: 0, width: 200, height: 120 };
    let offset = 0;
    const frame = position => {
        ui.begin(fieldInput(position), rect);
        offset = ui.scroll('form', viewport, 600, content => {
            for (let i = 0; i < 10; i++)
                ui.field('field' + i, { x: 0, y: content.y + i * 60, width: 180, height: 48 }, { value: '' });
        });
        ui.end();
    };
    textInputStarts.length = 0;
    frame(pointer(40, 100, true, true, 'touch'));
    frame(pointer(40, 70, false, true, 'touch'));
    frame(pointer(40, 30, false, true, 'touch'));
    frame(pointer(40, 30, false, false, 'touch'));
    assert.equal(textInputStarts.length, 0);
    assert.equal(ui.focus, null);
    assert.ok(offset > 50, 'scrolled ' + offset);
});

test('on touch, scrolling keeps the field being typed in; a tap elsewhere ends it', () => {
    const ui = reduced(createUI());
    const viewport = { x: 0, y: 0, width: 200, height: 120 };
    const frame = position => {
        ui.begin(fieldInput(position), rect);
        ui.scroll('form', viewport, 600, content => {
            for (let i = 0; i < 10; i++)
                ui.field('field' + i, { x: 0, y: content.y + i * 60, width: 180, height: 48 }, { value: '' });
        });
        ui.end();
    };
    frame(pointer(40, 24, true, true, 'touch'));
    frame(pointer(40, 24, false, false, 'touch'));
    assert.equal(ui.focus, 'field0');
    // A drag starting on another field: the focus and keyboard stay.
    frame(pointer(40, 100, true, true, 'touch'));
    frame(pointer(40, 60, false, true, 'touch'));
    frame(pointer(40, 60, false, false, 'touch'));
    assert.equal(ui.focus, 'field0');
    // A tap on empty space ends editing.
    frame(pointer(195, 110, true, true, 'touch'));
    frame(pointer(195, 110, false, false, 'touch'));
    assert.equal(ui.focus, null);
});

test('the layout shrinks above the on-screen keyboard and reveals the focused field', () => {
    withPlatform({ preferences: { keyboardInset: 0 } }, (calls, preferences) => {
        const ui = reduced(createUI());
        let offset = 0;
        const frame = position => {
            ui.begin(fieldInput(position));
            const view = ui.bounds;
            offset = ui.scroll('form', view, 1200, content => {
                for (let i = 0; i < 20; i++)
                    ui.field('field' + i, { x: 0, y: content.y + i * 60, width: 180, height: 48 }, { value: '' });
            });
            ui.end();
            return view;
        };
        assert.equal(frame(pointer(0, 0, false, false)).height, 480);
        // Tap the field at y 420..468, low on the 480 px screen.
        frame(pointer(40, 440, true, true, 'touch'));
        frame(pointer(40, 440, false, false, 'touch'));
        assert.equal(ui.focus, 'field7');
        preferences.keyboardInset = 200; // the keyboard covers the bottom 200 px
        assert.equal(frame(pointer(40, 440, false, false, 'touch')).height, 280);
        frame(pointer(40, 440, false, false, 'touch'));
        // field7 (420..468 in content) now ends above the 280 px view.
        assert.ok(420 + 48 - offset <= 280, 'offset ' + offset);
        preferences.keyboardInset = 0;
        assert.equal(frame(pointer(40, 440, false, false, 'touch')).height, 480);
    });
});
