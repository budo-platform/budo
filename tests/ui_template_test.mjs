// Runs the main.js of a `budo init --template ui` project against a recording
// canvas: it must draw its screen, and clicking "Say hello" must show a toast.
//
//   node tests/ui_template_test.mjs <project directory>

import assert from 'node:assert/strict';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const project = resolve(process.argv[2] || '.');
let calls = [];
let pending = null;
let input = null;
const record = name => (...args) => { calls.push([name, ...args]); };
const metrics = (text, width, size) => ({ width: Math.min(String(text).length * size * 0.5, width), height: size * 1.25, lines: 1 });

globalThis.sys = {
    canvas: new Proxy({
        measureText: (text, size) => String(text).length * size * 0.5,
        measureParagraph: metrics,
        drawParagraph: (text, x, y, width, size) => { calls.push(['drawText', text, x, y]); return metrics(text, width, size); },
    }, { get: (target, name) => target[name] || (target[name] = record(name)) }),
    input: { get: () => input, isKeyPressed: () => false, startTextInput: () => { }, updateTextInput: () => { }, stopTextInput: () => { } },
    window: { getWidth: () => 800, getHeight: () => 600, getDisplayDensity: () => 1 },
    animation: { requestFrame: callback => { pending = callback; }, waitForInput: callback => { pending = callback; } },
};

let time = 0;
function frame(pointer) {
    time += 1 / 60;
    input = {
        pointer: { id: 1, type: 'mouse', pressed: false, down: false, ...pointer },
        keyboard: {}, mouse: {}, totalTime: time, deltaTime: 1 / 60, text: '', textEdit: null,
        composition: { active: false, changed: false, text: '', selectionEnd: 0 },
    };
    assert.ok(pending, 'main.js asks for the next frame');
    const callback = pending;
    pending = null;
    calls = [];
    callback(time * 1000);
    return calls;
}
const texts = drawn => drawn.filter(call => call[0] === 'drawText').map(call => call[1]);

await import(pathToFileURL(join(project, 'main.js')).href);
let drawn = [];
for (let i = 0; i < 5; i++) drawn = frame({ x: 2, y: 2 });
for (const text of ['Hello, Budo', 'Notifications', 'Volume', 'System', 'Say hello'])
    assert.ok(texts(drawn).includes(text), `the starter draws "${text}"`);

const button = drawn.find(call => call[0] === 'drawText' && call[1] === 'Say hello');
const [x, y] = [button[2] + 8, button[3] - 4];
frame({ x, y });
frame({ x, y, pressed: true, down: true });
frame({ x, y });
for (let i = 0; i < 30; i++) drawn = frame({ x, y });
assert.ok(texts(drawn).includes('Hello!'), 'clicking "Say hello" shows a toast');
console.log('ui_template_test: ok');
