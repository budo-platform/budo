import assert from 'node:assert/strict';

let pending, nodes = [], actions = [], width = 960, height = 640, drawn = [], highlights = [];
let fillColor, inputSession, pressedKey;
let pointer = { x: -1, y: -1, type: 'mouse' }, wheelY = 0, fileOffset = 0;
const data = new TextEncoder().encode(Array.from({ length: 2400 }, (_, i) => `Line ${i}\n`).join(''));
const unicodeText = 'A\u00e9\u{1f642}Z';
const unicodeBytes = new TextEncoder().encode(unicodeText);
const reads = [];
const settings = new Map();
let lastDocument;
const noop = () => {};
const measure = (text, size) => Array.from(text).length * size / 2;
const paragraph = (text, width, size) => ({ width: Math.min(width, measure(text, size)), height: size, lines: 1 });
globalThis.sys = {
    canvas: new Proxy({
        measureText: measure,
        measureParagraph: paragraph,
        drawParagraph: (text, x, y, width, size) => paragraph(text, width, size),
        drawText: (text, x, y) => drawn.push({ text, x, y }),
        setFillColor: color => { fillColor = color; },
        setStrokeColor: () => { fillColor = null; },
        drawRoundRect: (x, y, width, height, radius) => {
            if (radius === 0 && /^#(?:006f67|3fb8ac)(?:14|2e)$/i.test(fillColor)) highlights.push({ x, y, width, height });
        },
    }, { get: (target, key) => target[key] || noop }),
    window: { getWidth: () => width, getHeight: () => height, getDisplayDensity: () => 1 },
    input: {
        get: () => ({ pointer, pointers: [], keyboard: {}, mouse: { wheelY }, focused: true }),
        isKeyPressed: key => key === pressedKey,
        startTextInput: session => { inputSession = session; },
        updateTextInput: session => { inputSession = session; },
        stopTextInput: () => { inputSession = null; },
    },
    font: { load: noop },
    db: {
        open: () => 1, execute: noop,
        query: (_, sql) => {
            if (sql.startsWith('PRAGMA')) return ['byte_offset', 'caret_byte', 'caret_nibble'].map(name => ({ name }));
            if (sql.includes('last_document')) return lastDocument ? [lastDocument] : [];
            return Array.from(settings, ([setting, value]) => ({ setting, value }));
        },
        run: (_, sql, key, value, caretByte, caretNibble) => {
            if (sql.includes('display_settings')) settings.set(key, value);
            if (sql.includes('last_document')) lastDocument = {
                path: key, byte_offset: value, caret_byte: caretByte, caret_nibble: caretNibble,
            };
        },
    },
    files: {
        list: () => [{ name: 'files', type: 'directory' }],
        isDirectory: path => path === 'files/folder',
        openRead: path => path === 'files/notes.txt' ? 1 : path === 'files/large.txt' ? 2 : path === 'files/unicode.txt' ? 3 : null,
        getReadInfo: handle => ({ size: handle === 1 ? 2 : handle === 3 ? unicodeBytes.length : data.length }),
        seek: (_, offset) => { fileOffset = offset; return true; },
        read: (handle, count) => {
            if (handle === 1) return Uint8Array.of(79, 75).buffer;
            if (handle === 3) return unicodeBytes.slice(fileOffset, fileOffset + count).buffer;
            reads.push({ start: fileOffset, length: count });
            return data.slice(fileOffset, fileOffset + count).buffer;
        },
        closeRead: noop,
    },
    accessibility: {
        isActive: () => true,
        update: value => { nodes = value; },
        takeActions: () => actions.splice(0),
    },
    animation: { requestFrame: cb => { pending = cb; }, waitForInput: cb => { pending = cb; } },
};

await import('../examples/text-editor/main.js');
let time = 0;
const frame = () => { drawn = []; highlights = []; pending(time += 17); };
const find = (label, role) => nodes.find(node => node.label === label && (!role || node.role === role));
function press(label, role = 'button') {
    const node = find(label, role);
    assert.ok(node, `expected button "${label}"`);
    actions.push({ id: node.id, action: 'press' });
    frame();
    frame();
}

frame();
const editor = () => find('UTF-8 text page', 'textbox');
assert.ok(find('File path', 'textbox').x < editor().x, 'the browser is left of the editor');
const openWidth = editor().width;
press('Hide file browser');
assert.equal(find('File path'), undefined);
assert.equal(settings.get('browser_visible'), '0');
assert.ok(editor().width > openWidth, 'hiding the sidebar returns space to the editor');
press('Show file browser');
assert.equal(settings.get('browser_visible'), '1');
actions.push({ id: find('File path', 'textbox').id, action: 'setValue', value: 'files/folder' });
frame();
press('Open');
assert.equal(settings.get('browser_directory'), 'files/folder', 'typing a directory updates the remembered location');
press('Up');
assert.equal(settings.get('browser_directory'), 'files', 'Up also updates the remembered location');
press('Save As');
assert.ok(find('Filename or mounted path', 'textbox').x < editor().x, 'Save As stays in the sidebar');
press('Cancel');
assert.ok(find('File path', 'textbox'));
actions.push({ id: find('File path', 'textbox').id, action: 'setValue', value: 'files/notes.txt' });
frame();
press('Open');
assert.equal(editor().value, 'OK');
assert.ok(find('File path', 'textbox'), 'opening a file leaves the wide sidebar visible');
assert.equal(find('Byte offset'), undefined);
assert.equal(find('Go', 'button'), undefined);
assert.equal(find('Hex bytes per row: Auto', 'tab'), undefined);

actions.push({ id: find('File path', 'textbox').id, action: 'setValue', value: 'files/large.txt' });
frame();
press('Open');
function scroll(editorNode, amount) {
    pointer = { ...pointer, x: editorNode.x + 30, y: editorNode.y + 30 };
    wheelY = amount;
    frame();
    wheelY = 0;
    frame();
}
scroll(editor(), -500);
assert.ok(reads.some(read => read.start > 0), 'Text wheel scrolling crosses the initial window');
assert.ok(!editor().value.startsWith('Line 0\n'));
scroll(editor(), 500);
assert.ok(editor().value.startsWith('Line 0\n'), 'Text scrolling can cross back to the start');
const view = editor();
const x = view.x + view.width - 16;
const top = view.y + 15;
pointer = { ...pointer, id: 1, x, y: top };
frame();
pointer = { ...pointer, pressed: true, down: true };
frame();
pointer = { ...pointer, pressed: false, y: view.y + view.height - 4 };
frame();
pointer = { ...pointer, down: false, x: -1, y: -1 };
frame();
assert.ok(editor().value.includes('Line 2399\n'), 'dragging the document thumb can reach the end');
scroll(editor(), 500);
scroll(editor(), 500);
scroll(editor(), 500);

press('Editing mode: Hex', 'tab');
function assertHexControlsShareRow() {
    const controls = [find('Byte offset', 'textbox'), find('Go', 'button'),
        ...['Auto', '16', '32'].map(value => find(`Hex bytes per row: ${value}`, 'tab'))];
    assert.ok(controls.every(Boolean), 'all Hex navigation controls are visible');
    const middle = controls[0].y + controls[0].height / 2;
    for (let i = 1; i < controls.length; i++) {
        const { x, y, height } = controls[i];
        assert.ok(y <= middle && middle < y + height, 'offset, Go and the row-width options share one row');
        assert.ok(controls[i - 1].x + controls[i - 1].width <= x, 'controls do not overlap horizontally');
    }
}
assertHexControlsShareRow();
const hexEditor = () => find('Hexadecimal byte page', 'textbox');
const rows = () => hexEditor().value.split('\n').map(line => line.split(' ').length);
actions.push({ id: find('Byte offset', 'textbox').id, action: 'setValue', value: '0' });
frame();
press('Go');
assert.ok(find('Hex bytes per row: Auto', 'tab').selected);
assert.equal(rows()[0], 16, 'Auto fits 16 columns with the sidebar at the initial width');
width = 2100;
height = 1350;
frame();
frame();
assert.equal(rows()[0], 32, 'Auto rounds down when there is room for 32 but not 64 columns');
width = 2500;
frame();
frame();
assert.equal(rows()[0], 64, 'Auto steps up once 64 columns fit');
const viewBounds = hexEditor();
const addresses = drawn.filter(({ text }) => /^\d+  0x[0-9A-F]+$/.test(text));
assert.ok(addresses.at(-1).y >= viewBounds.y + viewBounds.height - 44, 'rows fill the taller viewport');
assert.equal(hexEditor().value.replace(/\s+/g, ''),
    Buffer.from(data.subarray(0, hexEditor().value.replace(/\s+/g, '').length / 2)).toString('hex').toUpperCase());
press('Hex bytes per row: 16', 'tab');
assert.equal(rows()[0], 16);
press('Hex bytes per row: 32', 'tab');
assert.equal(rows()[0], 32);
assert.ok(find('Hex bytes per row: 32', 'tab').selected);
const firstHexRow = drawn.find(({ text }) => /^(?:[0-9A-F]{2} ){31}[0-9A-F]{2}$/.test(text));
pointer = { ...pointer, x: firstHexRow.x + 7, y: firstHexRow.y - 5, pressed: true, down: true };
frame();
pointer = { ...pointer, pressed: false, down: false };
frame();
assert.equal(inputSession.selectionStart, 1, 'focus the low nibble of the first byte');
assert.equal(highlights.length, 2, 'both columns highlight the active row');
const hexReads = reads.length;
scroll(hexEditor(), -500);
assert.ok(reads.slice(hexReads).some(read => read.start > 0), 'Hex wheel scrolling crosses the initial window');
assert.equal(highlights.length, 0, 'scrolling out of the byte buffer does not highlight an unrelated row');
assert.equal(rows()[0], 32, 'wider rows persist when the editor loads another window');
assert.ok(reads.every(read => read.length <= 8192), 'scrolling does not load the whole file');
scroll(hexEditor(), 500);
assert.equal(highlights.length, 2, 'scrolling back restores the highlight in both columns');
assert.equal(inputSession.selectionStart, 1, 'the low nibble is still active after scrolling back');
const hexBounds = hexEditor();
const thumbX = hexBounds.x + hexBounds.width - 16;
const thumbTop = hexBounds.y + 15;
const thumbBottom = hexBounds.y + hexBounds.height - 15;
pointer = { ...pointer, id: 1, x: thumbX, y: thumbTop, pressed: true, down: true };
frame();
pointer = { ...pointer, pressed: false, y: thumbBottom + 100 };
frame();
frame();
assert.equal(highlights.length, 0, 'dragging the thumb away hides the active row');
pointer = { ...pointer, y: thumbTop - 100 };
frame();
pointer = { ...pointer, down: false, x: -1, y: -1 };
frame();
assert.equal(highlights.length, 2, 'dragging the thumb back restores both highlights');
assert.equal(inputSession.selectionStart, 1, 'grabbing the thumb does not replace the active byte');
press('Hex bytes per row: Auto', 'tab');
assert.equal(rows()[0], 64, 'returning to Auto restores the fitted width');
press('Editing mode: Text', 'tab');
assert.equal(find('Byte offset'), undefined);
assert.equal(find('Go', 'button'), undefined);

width = 450;
height = 640;
frame();
assert.ok(find('File path', 'textbox'));
assert.equal(editor(), undefined, 'on a narrow window the browser has room to browse');
press('New');
assert.ok(editor(), 'creating a file on a narrow window brings back the editor');
assert.ok(find('Show file browser', 'button'));
assert.equal(settings.get('browser_visible'), '0', 'the narrow sidebar retract stays remembered');
assert.deepEqual(lastDocument, { path: '', byte_offset: 0, caret_byte: null, caret_nibble: 0 }, 'New clears the saved file positions');

width = 310;
frame();
press('Editing mode: Hex', 'tab');
assertHexControlsShareRow();
assert.ok(find('Byte offset', 'textbox').width >= 80, 'the offset field stays usable in the narrow layout');
assert.ok(find('Editing mode: Hex', 'tab').y < find('Byte offset', 'textbox').y, 'mode selection moves to its own row');
press('Editing mode: Text', 'tab');

width = 2100;
height = 1350;
press('Show file browser');
actions.push({ id: find('File path', 'textbox').id, action: 'setValue', value: 'files/large.txt' });
frame();
press('Open');
press('Editing mode: Hex', 'tab');
actions.push({ id: find('Byte offset', 'textbox').id, action: 'setValue', value: '10000' });
frame();
press('Go');
const topAddress = () => drawn.find(({ text }) => /^\d+  0x[0-9A-F]+$/.test(text))?.text;
const addressBeforeRestart = topAddress();
time += 150;
frame();
assert.ok(lastDocument.byte_offset > 8192, 'the saved scroll position is a byte offset beyond the first buffer');
assert.equal(Number(addressBeforeRestart.split(' ')[0]), lastDocument.byte_offset);
await import('../examples/text-editor/main.js?restart');
frame();
frame();
assert.equal(topAddress(), addressBeforeRestart, 'the reopened editor shows the same top row');

press('Editing mode: Text', 'tab');
scroll(editor(), -2);
time += 150;
frame();
const topLine = () => drawn.find(({ text }) => /^Line \d+$/.test(text))?.text;
const lineBeforeRestart = topLine();
assert.ok(lineBeforeRestart && lineBeforeRestart !== 'Line 0');
await import('../examples/text-editor/main.js?restart-text');
frame();
assert.equal(topLine(), lineBeforeRestart, 'Text mode restores the first visible line after wheel scrolling');

actions.push({ id: find('File path', 'textbox').id, action: 'setValue', value: 'files/unicode.txt' });
frame();
press('Open');
function clickAt(x, y) {
    pointer = { ...pointer, x, y };
    frame();
    pointer = { ...pointer, pressed: true, down: true };
    frame();
    pointer = { ...pointer, pressed: false, down: false };
    frame();
}
function clickMode(mode) {
    const tab = find(`Editing mode: ${mode}`, 'tab');
    clickAt(tab.x + tab.width / 2, tab.y + tab.height / 2);
    frame();
}
const textRow = drawn.find(({ text }) => text === unicodeText);
clickAt(textRow.x + 14, textRow.y - 5);
assert.equal(inputSession.selectionStart, 2, 'the Text cursor is before the four-byte character');
clickMode('Hex');
assert.equal(inputSession.selectionStart, 9, 'the Hex cursor is on the character’s first byte at offset 3');
const hexRow = drawn.find(({ text }) => text === '41 C3 A9 F0 9F 99 82 5A');
clickAt(hexRow.x + 16 * 7, hexRow.y - 5);
assert.equal(inputSession.selectionStart, 16, 'select the low nibble of a UTF-8 continuation byte');
clickMode('Text');
assert.equal(inputSession.selectionStart, 2, 'a continuation byte maps to the start of its UTF-8 character');
time += 150;
frame();
assert.equal(lastDocument.caret_byte, 5, 'the saved caret is independent of the scroll offset');
assert.equal(lastDocument.byte_offset, 0);
await import('../examples/text-editor/main.js?restart-caret');
frame();
frame();
assert.equal(inputSession.selectionStart, 2, 'reload restores the visible Text caret');
clickMode('Hex');
assert.equal(inputSession.selectionStart, 16, 'switching back after reload restores the exact nibble');

width = 960;
height = 640;
frame();
clickMode('Text');
actions.push({ id: find('File path', 'textbox').id, action: 'setValue', value: 'files/large.txt' });
frame();
press('Open');
function pageKey(key) {
    const top = () => topAddress() ?? topLine();
    const before = top();
    pressedKey = key;
    frame();
    const first = top();
    pressedKey = undefined;
    frame();
    const second = top();
    frame();
    const after = top();
    assert.ok([before, first, second, after].filter((row, i, rows) => i && row !== rows[i - 1]).length <= 1,
        `one Page key must not show multiple viewport positions: ${[before, first, second, after].join(' → ')}`);
}
const textStart = drawn.find(({ text }) => text === 'Line 0');
clickAt(textStart.x + 7, textStart.y - 5);
const activeTextLine = () => {
    const before = inputSession.text.slice(0, inputSession.selectionStart);
    return Number(inputSession.text.slice(before.lastIndexOf('\n') + 1).match(/^Line (\d+)/)[1]);
};
pageKey(78);
const pageRows = activeTextLine();
assert.ok(pageRows > 1, 'Page Down advances the Text caret by a screen');
assert.notEqual(topLine(), 'Line 0', 'the Text viewport follows the caret down');
assertCentered(editor());
assert.equal(inputSession.text[inputSession.selectionStart], 'i', 'Text keeps the caret column');
pageKey(75);
assert.equal(activeTextLine(), 0, 'Page Up returns the Text caret to the first row');
const textBuffer = reads.at(-1).start;
const textPageTurns = 55;
for (let i = 1; i <= textPageTurns; i++) {
    pageKey(78);
    assert.equal(activeTextLine(), i * pageRows, 'Text paging crosses buffers without changing the row step');
}
assert.ok(reads.at(-1).start > textBuffer, 'Text paging loaded a new buffer');
for (let i = textPageTurns - 1; i >= 0; i--) {
    pageKey(75);
    assert.equal(activeTextLine(), i * pageRows, 'Text paging returns across the buffer boundary');
}

clickMode('Hex');
press('Hex bytes per row: 16', 'tab');
const hexStart = drawn.find(({ text }) => /^(?:[0-9A-F]{2} ){15}[0-9A-F]{2}$/.test(text));
clickAt(hexStart.x + 7, hexStart.y - 5);
const activeHexByte = () => reads.at(-1).start + Math.floor(inputSession.selectionStart / 3);
const firstByte = activeHexByte();
pageKey(78);
const byteStep = activeHexByte() - firstByte;
assert.equal(byteStep, pageRows * 16, 'Page Down advances the Hex caret by the same number of rows');
assert.ok(Number(topAddress().split(' ')[0]) > firstByte, 'the Hex viewport follows the caret down');
assert.equal(highlights.length, 2, 'the moved cursor row stays visible in both columns');
assertCentered(hexEditor());
const startingBuffer = reads.at(-1).start;
for (let i = 2; i <= 20; i++) {
    pageKey(78);
    assert.equal(activeHexByte(), firstByte + i * byteStep, 'Page Down keeps advancing the caret across buffers');
    assert.equal(inputSession.selectionStart % 3, 1, 'the low nibble remains active');
    assertCentered(hexEditor());
}
assert.ok(reads.at(-1).start > startingBuffer, 'the Page keys cross an internal buffer boundary');
for (let i = 19; i >= 0; i--) {
    pageKey(75);
    assert.equal(activeHexByte(), firstByte + i * byteStep, 'Page Up crosses back without losing the caret position');
    if (i > 0) assertCentered(hexEditor());
}
assert.equal(Number(topAddress().split(' ')[0]), 0, 'Page Up clamps centering at the start of the file');
function assertCentered(editorNode) {
    const caret = inputSession.caret;
    const mid = editorNode.y + editorNode.height / 2;
    assert.ok(Math.abs(caret.y + caret.height / 2 - mid) <= 1, 'the active row is vertically centered');
}
clickMode('Text');
assert.equal(topLine(), 'Line 0', 'centering near the beginning clamps at the start of the file');
for (let i = 0; i < 8; i++) pageKey(78);
const targetLine = activeTextLine();
clickMode('Hex');
assertCentered(hexEditor());
press('Hex bytes per row: Auto', 'tab');
clickMode('Text');
assert.equal(activeTextLine(), targetLine);
assertCentered(editor());
width = 2100;
frame();
clickMode('Hex');
frame();
assert.equal(rows()[0], 32, 'Hex adjusts its Auto columns after the mode switch');
assertCentered(hexEditor());
console.log('text_editor_browser_test: ok');
