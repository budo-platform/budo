import assert from 'node:assert/strict';
import test from 'node:test';

test('UI enables seeking only after readiness and draws video after the canvas', async () => {
    let nextFrame, callbacks;
    let width = 800, height = 600;
    let pointer = { id: 1, x: 0, y: 0, pressed: false, down: false, type: 'mouse' };
    const calls = [];
    const keys = new Set();
    const noop = () => {};
    globalThis.sys = {
        files: { exists: () => true },
        window: { getWidth: () => width, getHeight: () => height, getDisplayDensity: () => 1 },
        input: { get: () => ({ pointer, mouse: { wheelY: 0 } }), isKeyPressed: key => keys.has(key) },
        animation: { requestFrame: fn => { nextFrame = fn; } },
        canvas: {
            clear: () => calls.push('clear'), save: noop, restore: noop, clipRect: noop,
            setFillColor: noop, setStrokeColor: noop, setStrokeWidth: noop,
            drawRoundRect: noop, drawRect: noop, drawLine: noop,
            drawText: noop, measureText: text => text.length * 8,
        },
        gl: {
            createProgram: () => 1, bindScreen: () => calls.push('bind'),
            useProgram: () => calls.push('use'), texture: () => calls.push('texture'),
            drawRegionImmediate: () => calls.push('draw'),
        },
        video: {
            preview: (a, b, cb) => { callbacks = cb; return 1; },
            seekPreview: (id, pts) => { calls.push(['seek', id, pts]); return true; },
            pausePreview: () => { calls.push('pause'); return true; },
            resumePreview: () => { calls.push('resume'); return true; },
            exportMp4: () => { calls.push('export'); return 2; },
            getError: () => '', closePreview: () => true,
        },
    };
    await import('./main.js');
    const frame = (x, y, down, pressed = down) => {
        pointer = { ...pointer, x, y, down, pressed };
        nextFrame();
    };
    frame(0, 0, false);
    frame(770, 396, true);
    assert.equal(calls.some(call => Array.isArray(call)), false);
    frame(770, 396, false);
    callbacks.onReady(2000000);
    callbacks.onFrame(3, 0);
    calls.length = 0;
    frame(0, 0, false);
    assert.deepEqual(calls.slice(-4), ['bind', 'use', 'texture', 'draw']);
    frame(600, 396, true);
    callbacks.onFrame(3, 500000);
    frame(770, 396, true, false);
    assert.equal(calls.some(call => Array.isArray(call)), false);
    frame(770, 396, false);
    assert.deepEqual(calls.filter(call => Array.isArray(call)), [['seek', 1, 1999999]]);
    // The accepted seek immediately removes the old borrowed texture.
    assert.notEqual(calls.at(-1), 'draw');
    frame(100, 436, true);
    frame(100, 436, false);
    assert.equal(calls.at(-1), 'pause');
    keys.add(44); // SDL/USB Space, not the ASCII space code.
    frame(100, 436, false);
    keys.clear();
    assert.equal(calls.at(-1), 'resume');
    width = 320; height = 480;
    frame(0, 0, false);
    frame(220, 326, true);
    frame(220, 326, false);
    assert.equal(calls.at(-1), 'export');
});
