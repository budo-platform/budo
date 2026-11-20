import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const source = readFileSync(new URL('../src/web/web_image.js', import.meta.url), 'utf8');
const workerSource = readFileSync(new URL('../src/web/web_image_worker.js', import.meta.url), 'utf8');

function workerFixture(decodedWidth = 17, decodedHeight = 13, sourceWidth = 131, sourceHeight = 97) {
    const memory = new ArrayBuffer(65536);
    const messages = [], decodes = [], rows = [];
    let cursor = 64;
    const decoder = {
        HEAPU8: new Uint8Array(memory), HEAP32: new Int32Array(memory),
        _malloc(length) { const result = cursor; cursor += (length + 3) & ~3; return result; },
        _free() { }, _image_free() { },
        _image_probe(bytes, length, width, height) {
            this.HEAP32[width >> 2] = sourceWidth;
            this.HEAP32[height >> 2] = sourceHeight;
            return 1;
        },
        _image_load_rgba8_scaled_from_memory(bytes, length, targetWidth, targetHeight, width, height) {
            decodes.push([targetWidth, targetHeight]);
            this.HEAP32[width >> 2] = decodedWidth;
            this.HEAP32[height >> 2] = decodedHeight;
            return this._malloc(decodedWidth * decodedHeight * 4);
        },
        _image_resize_rgba8_row(source, sourceWidth, sourceHeight, output, width, height, row) {
            rows.push([sourceWidth, sourceHeight, width, height, row]);
            this.HEAPU8.fill(127, output + row * width * 4, output + (row + 1) * width * 4);
        },
    };
    const context = { BudoImageDecoder: async () => decoder, self: { postMessage: message => messages.push(message) } };
    runInNewContext(workerSource, context);
    return { messages, decodes, rows, send: data => context.self.onmessage({ data }) };
}

test('browser worker resizes from scaled JPEG dimensions and preserves source metadata', async () => {
    const worker = workerFixture();
    await worker.send({ type: 'probe', request: 1, bytes: new ArrayBuffer(4), maxWidth: 16, maxHeight: 16, maxPixels: 24000000 });
    assert.deepEqual({ ...worker.messages[0] }, { type: 'metadata', request: 1, sourceWidth: 131, sourceHeight: 97, width: 16, height: 11 });
    await worker.send({ type: 'decode', request: 1 });
    assert.deepEqual(worker.decodes, [[16, 11]]);
    assert.equal(worker.rows.length, 11);
    assert.deepEqual(worker.rows[0], [17, 13, 16, 11, 0]);
    assert.equal(worker.messages[1].type, 'complete');
    assert.equal(worker.messages[1].pixels.byteLength, 16 * 11 * 4);
    assert.ok(new Uint8Array(worker.messages[1].pixels).every(value => value === 127));
});

test('browser worker accepts full-resolution phone photos and still enforces lower pixel limits', async () => {
    for (const maxPixels of [256000000, 48000000]) {
        const worker = workerFixture(17, 13, 20000, 12000);
        await worker.send({ type: 'probe', request: 1, bytes: new ArrayBuffer(4), maxWidth: 16, maxHeight: 16, maxPixels });
        if (maxPixels === 256000000) {
            assert.deepEqual({ ...worker.messages[0] }, { type: 'metadata', request: 1, sourceWidth: 20000, sourceHeight: 12000, width: 16, height: 9 });
        } else {
            assert.equal(worker.messages[0].code, 'image.pixel_limit');
        }
        assert.equal(worker.decodes.length, 0);
    }
    for (const [width, height] of [[32769, 1], [1, 32769], [20000, 13000]]) {
        const worker = workerFixture(17, 13, width, height);
        await worker.send({ type: 'probe', request: 1, bytes: new ArrayBuffer(4), maxWidth: 16, maxHeight: 16, maxPixels: 256000000 });
        assert.equal(worker.messages[0].code, 'image.pixel_limit');
        assert.equal(worker.decodes.length, 0);
    }
});

test('browser worker enforces original pixel limits and rejects invalid scaled dimensions', async () => {
    const limited = workerFixture();
    await limited.send({ type: 'probe', request: 1, bytes: new ArrayBuffer(4), maxWidth: 16, maxHeight: 16, maxPixels: 256 });
    assert.equal(limited.messages[0].code, 'image.pixel_limit');
    assert.equal(limited.decodes.length, 0);
    for (const [width, height] of [[15, 13], [17, 10], [132, 13], [17, 98]]) {
        const worker = workerFixture(width, height);
        await worker.send({ type: 'probe', request: 1, bytes: new ArrayBuffer(4), maxWidth: 16, maxHeight: 16, maxPixels: 24000000 });
        await worker.send({ type: 'decode', request: 1 });
        assert.equal(worker.messages[1].code, 'image.decode_failed');
        assert.equal(worker.rows.length, 0);
    }
});

function fixture(overrides = {}) {
    const workers = [];
    const revoked = [];
    class Worker {
        constructor() { workers.push(this); }
        postMessage(message) { this.message = message; }
        terminate() { this.stopped = true; }
        receive(data) { this.onmessage({ data }); }
    }
    const memory = new ArrayBuffer(256);
    const context = {
        Module: {}, Worker, WebAssembly: {}, Blob: class { },
        URL: { createObjectURL: () => 'blob:decoder', revokeObjectURL: url => revoked.push(url) },
        FS: { readFile: () => 'decoder' }, HEAPU8: new Uint8Array(memory), HEAP32: new Int32Array(memory),
        ...overrides
    };
    runInNewContext(source, context);
    return { backend: context.Module.budoImageBackend, workers, revoked, ...context };
}

test('browser bridge bounds workers and copies only the submitted byte range', () => {
    const { backend, workers, HEAPU8, HEAP32 } = fixture();
    const service = backend.create();
    HEAPU8.set([1, 2, 3, 4], 20);
    assert.equal(backend.submit(service, 1, 21, 2, 0, 0, 100), 1);
    assert.deepEqual([...new Uint8Array(workers[0].message.bytes)], [2, 3]);
    HEAPU8.fill(0);
    assert.deepEqual([...new Uint8Array(workers[0].message.bytes)], [2, 3]);
    assert.equal(backend.submit(service, 2, 0, 1, 0, 0, 100), 1);
    assert.equal(backend.submit(service, 3, 0, 1, 0, 0, 100), 0);
    workers[0].receive({ request: 1, type: 'metadata', width: 1, height: 2, sourceWidth: 3, sourceHeight: 4 });
    assert.equal(backend.status(service, 1, 64), 1);
    assert.deepEqual([...HEAP32.slice(16, 20)], [1, 2, 3, 4]);
    assert.equal(backend.status(service, 1, 64), 0);
    backend.decode(service, 1);
    assert.equal(workers[0].message.type, 'decode');
    workers[0].receive({ request: 1, type: 'complete', pixels: new Uint8Array([9, 8, 7, 6]).buffer });
    assert.equal(backend.status(service, 1, 64), 2);
    assert.equal(backend.copy(service, 1, 100, 8), 0);
    assert.equal(backend.copy(service, 1, 100, 4), 1);
    assert.deepEqual([...HEAPU8.slice(100, 104)], [9, 8, 7, 6]);
    assert.equal(backend.copy(service, 1, 100, 4), 0);
    backend.destroy(service);
});

test('cancellation and peer teardown reject late worker messages', () => {
    const { backend, workers, revoked } = fixture();
    const first = backend.create();
    const peer = backend.create();
    backend.submit(first, 1, 0, 1, 0, 0, 100);
    const stale = workers[0];
    backend.cancel(first, 1);
    assert.ok(stale.stopped);
    backend.submit(first, 2, 0, 1, 0, 0, 100);
    stale.receive({ request: 1, type: 'complete', pixels: new ArrayBuffer(4) });
    assert.equal(backend.status(first, 2, 64), 0);
    backend.submit(peer, 1, 0, 1, 0, 0, 100);
    backend.destroy(first);
    workers[4].receive({ request: 2, type: 'complete', pixels: new ArrayBuffer(4) });
    workers[2].receive({ request: 1, type: 'error', code: 'image.invalid_data' });
    assert.equal(backend.status(peer, 1, 64), 3);
    backend.destroy(peer);
    backend.destroy(peer);
    assert.equal(revoked.length, 2);
    assert.ok(workers.every(worker => worker.stopped));
});

test('worker failures become operational errors and startup failures clean up', () => {
    const { backend, workers } = fixture();
    const service = backend.create();
    backend.submit(service, 1, 0, 1, 0, 0, 100);
    workers[0].onerror({ preventDefault() { } });
    assert.equal(backend.status(service, 1, 64), 6);
    assert.ok(workers[0].stopped);
    backend.cancel(service, 1);
    workers[1].onerror({ preventDefault() { } });
    backend.submit(service, 2, 0, 1, 0, 0, 100);
    assert.equal(workers.length, 3);
    assert.equal(backend.status(service, 2, 64), 0);
    backend.destroy(service);
    const blocked = fixture({ Worker: class { constructor() { throw new Error('CSP'); } } });
    assert.equal(blocked.backend.create(), 0);
    assert.equal(blocked.revoked.length, 1);
    const unavailable = fixture({ Worker: undefined });
    assert.equal(unavailable.backend.available(), false);
    assert.equal(unavailable.backend.create(), 0);
});