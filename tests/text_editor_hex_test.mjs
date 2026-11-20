// Run with: node tests/text_editor_hex_test.mjs
import assert from 'node:assert/strict';
import { formatByteAddress, formatHex, hexCells, hexRowAddresses, parseHex } from '../examples/text-editor/document.js';
import { install } from '../examples/text-editor/editor-ui/lib/widgets/textarea.js';
import { parseColor } from '../examples/text-editor/editor-ui/lib/color.js';

const original = '41  00 ff 42\n43 44';
const model = { value: original };
const size = 10, advance = 6;
const rect = { x: 0, y: 0, width: 580, height: 190 };
const offset = 0x100000000;
const options = {
    size, resetKey: 0, byteAppend: true, zoom: { factor: 1 },
    byteView: { scan: hexCells, parse: parseHex, format: formatHex },
    gutter: { sample: formatByteAddress(offset + 512, 9), labels: (text, starts) => hexRowAddresses(text, starts, offset, 9) },
};
let keys = [], time = 0, text, blocks, fills;
const warnings = [];
const measure = (str, fontSize) => str.length * fontSize * advance / size;
globalThis.sys = {
    canvas: { measureText: measure },
    input: { isKeyPressed: key => keys.includes(key), startTextInput() {}, stopTextInput() {}, updateTextInput() {} },
};
const ctx = {
    theme: { font: size, accent: '#008888', focus: '#0088FF' },
    focus: null, active: 'editor', motion: new Map(),
    px: x => x, sp: x => x, fullId: id => id, toScreen: r => r, inside: () => true, measure,
    interact: id => ({ id, hovered: true }), springAt: () => 0, colorAt: (_, color) => color,
    snapAt() {}, revealAboveKeyboard() {}, pushClip() {}, popClip() {}, line() {},
    fill: (r, color) => fills.push({ ...r, color }),
    text: (str, x, y, fontSize) => text.push({ str, x, y, fontSize }),
    outline: (r, color) => blocks.push({ ...r, color }),
    keyRepeated: key => keys.includes(key),
};
const ui = {
    scroll: (_, r, height, draw) => draw({ ...r, width: r.width - (height > r.height ? 12 : 0) }),
    horizontalScrollbar: (_, _r, value) => value,
    toast: message => warnings.push(message),
};
install(ui, ctx);
function frame(overrides = {}, pressedKeys = []) {
    text = []; blocks = []; fills = []; keys = pressedKeys;
    ctx.input = {
        focused: true, totalTime: ++time, pointer: { type: 'mouse' }, pointers: [],
        keyboard: {}, mouse: {}, ...overrides,
    };
    ui.textArea('editor', rect, model, options);
}
const drawn = str => text.find(row => row.str === str);
function click(row, column = 0) {
    frame({ pointer: { type: 'mouse', x: row.x + column * row.fontSize * advance / size + 1,
        y: row.y - 4, pressed: true, down: true } });
}
const cursors = () => blocks.slice(-2);
const lines = () => fills.filter(r => r.height === 14 && r.color?.startsWith('#008888'));
const alpha = r => parseColor(r.color)[3];

frame();
assert.equal(drawn('4294967296  0x100000000').y, drawn('A..B').y);
assert.equal(drawn('4294967300  0x100000004').y, drawn('CD').y);
assert.equal(drawn('A..B').y, drawn('41  00 ff 42').y);
click(drawn('41  00 ff 42'), 1);
assert.ok(alpha(cursors()[0]) > alpha(cursors()[1]), 'hex cursor is stronger');
assert.ok(alpha(lines()[0]) > alpha(lines()[1]), 'hex line is stronger');
assert.equal(cursors()[1].x, drawn('A..B').x, 'both nibbles map to the first byte');

const hexX = drawn(original.split('\n')[0]).x;
frame({}, [79]);
assert.equal(cursors()[0].x, hexX + 4 * advance, 'Right skips both spaces to the next byte');
frame({}, [79]);
assert.equal(cursors()[0].x, hexX + 5 * advance, 'Right still reaches the second digit');
frame({}, [80]);
frame({}, [80]);
assert.equal(cursors()[0].x, hexX + advance, 'Left skips spaces in the opposite direction');
click(drawn('41  00 ff 42'), 2);
assert.equal(cursors()[0].x, hexX + 4 * advance, 'clicking a separator snaps to a digit');
click(drawn('41  00 ff 42'), 11);
const firstCursorY = cursors()[0].y;
frame({}, [79]);
assert.equal(cursors()[0].y - firstCursorY, drawn('43 44').y - drawn('41  00 ff 42').y, 'Right jumps across the newline');
frame({}, [80]);
assert.equal(cursors()[0].x, hexX + 11 * advance, 'Left returns to the preceding byte across the newline');
click(drawn('41  00 ff 42'), 4);
frame({}, [42]);
assert.equal(model.value, '4  00 ff 42\n43 44', 'Backspace removes the preceding digit rather than a separator');
frame({ keyboard: { ctrl: true } }, [29]);
assert.equal(model.value, original);

click(drawn('A..B'), 1);
assert.ok(alpha(cursors()[1]) > alpha(cursors()[0]), 'ASCII cursor is stronger');
assert.ok(alpha(lines()[1]) > alpha(lines()[0]), 'ASCII line is stronger');
frame({ text: 'z' });
assert.equal(model.value, '41  7A ff 42\n43 44', 'overwrite preserves length and hex whitespace');
assert.ok(drawn('Az.B'));
frame({ keyboard: { ctrl: true } }, [29]);
assert.equal(model.value, original, 'ASCII overwrite can be undone');

click(drawn('41  00 ff 42'), 1);
frame({ keyboard: { shift: true } }, [79]);
frame({ text: '5' });
assert.equal(model.value, '45  00 ff 42\n43 44', 'selecting the last digit of a byte leaves the separator intact');
assert.ok(drawn('E..B'), 'changing a hex digit immediately updates ASCII');
frame({ keyboard: { ctrl: true } }, [29]);
assert.equal(model.value, original);

frame({ nativeTextEditing: true, textEdit: { text: original, selectionStart: 2, selectionEnd: 2 } });
assert.equal(cursors()[0].x, hexX + 4 * advance, 'a native caret on whitespace snaps to the next digit');

click(drawn('CD'), 2);
options.byteAppend = false;
frame({ text: '!' });
assert.equal(model.value, original, 'cannot append to a non-final page');
assert.match(warnings.at(-1), /End of this page/);
options.byteAppend = true;
frame({ nativeTextEditing: true, textEdit: { text: '!' } });
assert.deepEqual([...parseHex(model.value)], [65, 0, 255, 66, 67, 68, 33]);
assert.ok(drawn('A..BCD!'));

model.value = '41 F';
options.resetKey++;
frame();
click(drawn('A?'), 1);
frame({ text: 'X' });
assert.equal(model.value, '41 F');
assert.match(warnings.at(-1), /complete byte pairs/);

model.value = formatHex(Uint8Array.from({ length: 16 }, (_, i) => 65 + i));
options.resetKey++;
options.zoom.factor = 3;
rect.width = 380;
rect.height = 380;
frame();
assert.ok(drawn('4294967296  0x100000000').x >= 12, 'both addresses fit in the gutter at high zoom');
const asciiRows = text.filter(row => /^[A-P]+$/.test(row.str));
assert.ok(asciiRows.length > 1, 'both columns share wrapped rows');
assert.equal(asciiRows.map(row => row.str).join(''), 'ABCDEFGHIJKLMNOP');
click(asciiRows.at(-1));
const selected = asciiRows.at(-1).str.charCodeAt(0) - 65;
frame({ text: 'x' });
assert.equal(parseHex(model.value)[selected], 120, 'click targets the right byte after zoom and wrap');

options.wrap = false;
frame({ keyboard: { ctrl: true } }, [77]);
const lastByte = cursors()[1];
assert.ok(lastByte.x >= 0 && lastByte.x + lastByte.width <= rect.width - 12, 'ASCII EOF cursor is revealed without wrap');
console.log('text_editor_hex_test: ok');
