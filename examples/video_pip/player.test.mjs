import assert from 'node:assert/strict';
import test from 'node:test';
import { createPlayer } from './player.js';

function harness() {
    const calls = [];
    let preview, exported;
    const video = {
        preview(a, b, callbacks) { preview = callbacks; calls.push(['preview']); return 1; },
        closePreview(id) { calls.push(['close', id]); return true; },
        seekPreview(id, pts) { calls.push(['seek', id, pts]); return true; },
        pausePreview(id) { calls.push(['pause', id]); return true; },
        resumePreview(id) { calls.push(['resume', id]); return true; },
        exportMp4(a, b, path, callbacks) { exported = callbacks; calls.push(['export']); return 2; },
        cancelExport(id) { calls.push(['cancel', id]); return true; },
        getError() { return 'busy'; },
    };
    const player = createPlayer(video, 'assets/a', 'assets/b', 'files/out');
    return { player, video, calls, preview: () => preview, exported: () => exported };
}

test('seeking waits for readiness, clamps to duration and invalidates borrowed texture', () => {
    const h = harness(), { player, calls } = h;
    player.start();
    assert.equal(player.seek(42), false);
    h.preview().onReady(2050000);
    h.preview().onFrame(9, 100000);
    player.toggle();
    assert.equal(player.state.paused, true);
    assert.equal(player.seek(9999999), true);
    assert.deepEqual(calls.at(-1), ['seek', 1, 2049999]);
    assert.equal(player.state.texture, 0);
    assert.equal(player.state.paused, true);
    assert.equal(player.state.positionUs, 2049999);
    assert.equal(player.state.previewStatus, 'Seeking from previous keyframe...');
    h.preview().onFrame(9, 2000000);
    assert.equal(player.state.positionUs, 2000000);
    assert.equal(player.state.texture, 9);
    assert.equal(player.state.seeking, false);
});

test('seek while playing can still pause; play after end restarts', () => {
    const h = harness(), { player, calls } = h;
    player.start();
    h.preview().onReady(10000000);
    player.seek(5000000);
    player.toggle();
    assert.deepEqual(calls.at(-1), ['pause', 1]);
    player.toggle();
    h.preview().onEnd();
    player.toggle();
    assert.deepEqual(calls.at(-1), ['seek', 1, 0]);
    assert.equal(player.state.paused, false);
    h.preview().onEnd();
    player.state.paused = true;
    player.toggle();
    assert.deepEqual(calls.slice(-2), [['seek', 1, 0], ['resume', 1]]);
});

test('failed seek and close retain the current frame; stale callbacks are ignored', () => {
    const h = harness(), { player, video } = h;
    player.start();
    const old = h.preview();
    old.onReady(5000000);
    old.onFrame(7, 1000);
    video.seekPreview = () => false;
    assert.equal(player.seek(2000), false);
    assert.equal(player.state.texture, 7);
    video.closePreview = () => false;
    assert.equal(player.close(), false);
    assert.equal(player.state.texture, 7);
    video.closePreview = () => true;
    player.close();
    player.start();
    old.onFrame(8, 2000);
    old.onError({ code: 'old', message: 'late' });
    assert.equal(player.state.texture, 0);
    assert.ok(player.state.preview);
});

test('export is single-flight, cancellation is single-shot, and terminal callbacks restore controls', () => {
    const h = harness(), { player, calls } = h;
    player.startExport();
    player.startExport();
    assert.deepEqual(calls, [['export']]);
    player.cancelExport();
    player.cancelExport();
    assert.deepEqual(calls, [['export'], ['cancel', 2]]);
    h.exported().onProgress(3);
    assert.equal(player.state.exportStatus, 'Cancellation requested...');
    h.exported().onError({ code: 'video.cancelled', message: 'Cancelled' });
    assert.equal(player.state.export, null);
    player.startExport();
    h.exported().onComplete('files/out');
    assert.equal(player.state.export, null);
    assert.match(player.state.exportStatus, /files\/out/);
});

test('busy open is retryable and synchronous terminal callbacks do not resurrect jobs', () => {
    const h = harness(), { player, video } = h;
    video.preview = () => 0;
    assert.equal(player.start(), false);
    assert.equal(player.state.preview, null);
    assert.equal(player.state.previewStatus, 'busy');
    video.preview = (a, b, callbacks) => {
        callbacks.onError({ code: 'bad', message: 'Failed' });
        return 12;
    };
    assert.equal(player.start(), true);
    assert.equal(player.state.preview, null);
    assert.match(player.state.previewStatus, /bad: Failed/);
    video.exportMp4 = (a, b, out, callbacks) => {
        callbacks.onComplete(out);
        return 3;
    };
    player.startExport();
    assert.equal(player.state.export, null);
    assert.match(player.state.exportStatus, /complete/);
});
