// MIDI behaviour of the MIDI Preset Saver: capture, presets, recording,
// playback, recall, and devices. main.ts runs as Budo runs it (through Budo's
// TypeScript stripper), with node's SQLite as the database and a scripted MIDI
// device that records everything sent.
//
//   BUDO_TS_STRIP=build/ts_strip_tool node --test examples/midi_preset_saver/midi.test.mjs
// (CTest runs it as midi_preset_saver.)

import assert from 'node:assert/strict';
import nodeTest from 'node:test';
import { execFileSync } from 'node:child_process';
import { cpSync, mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

// node:sqlite (Node 22.5+) stands in for sys.db; older Node skips these tests.
let DatabaseSync = null;
try {
    ({ DatabaseSync } = await import('node:sqlite'));
} catch (error) {
    DatabaseSync = null;
}
const test = DatabaseSync ? nodeTest : nodeTest.skip;

const here = dirname(fileURLToPath(import.meta.url));
const stripper = process.env.BUDO_TS_STRIP;
if (!stripper) throw new Error('Set BUDO_TS_STRIP to the ts_strip_tool executable');

const INTERNALS = [
    'createPreset', 'updatePreset', 'snapshotCommands', 'clearCapture', 'startSequenceRecording', 'stopSequenceRecording',
    'savePresetSequence', 'playPresetSequence', 'stopSequencePlayback', 'prepareSequencePlaybackEvents', 'recallPreset',
    'recallLivePreset', 'chooseInput', 'chooseOutput', 'selectPreset', 'loadPresets', 'decodePreset', 'decodeNoteSequence',
    'validateDatabaseExport', 'buildDatabaseExport', 'applyDatabaseExport',
];

/** A fresh app instance: its own database, device, and clock. */
async function launch({ inputs = ['Keys'], outputs = ['Synth'] } = {}) {
    const log = [];        // ['send', bytes] and ['db', sql], in order
    const midi = {
        inputs: inputs.map((name, id) => ({ id, name })),
        outputs: outputs.map((name, id) => ({ id, name })),
        input: null, output: null, onInput: null, onChange: null,
    };
    const db = new DatabaseSync(':memory:');
    let lastInsertId = 0;
    let frameCallback = null;
    const noop = () => { };
    const canvas = new Proxy({ measureText: text => String(text).length * 8 }, {
        get: (target, name) => target[name] || noop,
    });
    globalThis.sys = {
        log: noop,
        db: {
            open: () => 1,
            execute: (handle, sql) => { log.push(['db', sql]); db.exec(sql); return true; },
            run: (handle, sql, ...params) => {
                log.push(['db', sql]);
                const result = db.prepare(sql).run(...params);
                lastInsertId = Number(result.lastInsertRowid);
                return result.changes;
            },
            query: (handle, sql, ...params) => db.prepare(sql).all(...params).map(row => ({ ...row })),
            lastInsertId: () => lastInsertId,
            getError: () => '',
        },
        midi: {
            NOTE_ON: 0x90, NOTE_OFF: 0x80, POLY_PRESSURE: 0xA0, CONTROL_CHANGE: 0xB0, PROGRAM_CHANGE: 0xC0,
            CHANNEL_PRESSURE: 0xD0, PITCH_BEND: 0xE0, SYSEX: 0xF0,
            refreshDevices: noop,
            getInputDevices: () => midi.inputs.slice(),
            getOutputDevices: () => midi.outputs.slice(),
            openInput: (index, callback) => { midi.input = midi.inputs[index].name; midi.onInput = callback; return 0; },
            closeInput: () => { midi.input = null; midi.onInput = null; },
            openOutput: index => { midi.output = midi.outputs[index].name; return 0; },
            closeOutput: () => { midi.output = null; },
            sendRaw: (handle, bytes) => { log.push(['send', Array.from(bytes), midi.output]); return midi.output !== null; },
            onDevicesChanged: callback => { midi.onChange = callback; },
        },
        capabilities: { midi: { available: true } },
        device: { keepScreenOn: () => true },
        gl: { createProgram: () => 1, bindScreen: noop, setUniform1f: noop, setUniform2f: noop, drawFullscreen: noop },
        font: { load: noop },
        canvas,
        path: { create: () => 1, addSvg: () => true },
        window: { getWidth: () => 1000, getHeight: () => 700, getDisplayDensity: () => 1 },
        input: {
            get: () => ({ pointer: { id: 1, x: -50, y: -50, pressed: false, down: false, type: 'mouse' },
                keyboard: {}, mouse: {}, totalTime: 0, deltaTime: 1 / 60, text: '', textEdit: null,
                composition: { active: false, changed: false, text: '', selectionEnd: 0 } }),
            isKeyPressed: () => false, isKeyDown: () => false,
            startTextInput: noop, updateTextInput: noop, stopTextInput: noop,
        },
        animation: { requestFrame: callback => { frameCallback = callback; }, waitForInput: callback => { frameCallback = callback; } },
        files: { list: () => [], saveText: () => true, pickText: () => true, readText: () => null, getError: () => '' },
    };

    // main.ts as Budo runs it, plus an export of the internals under test.
    const directory = mkdtempSync(join(tmpdir(), 'midi-preset-saver-'));
    cpSync(join(here, 'ui'), join(directory, 'ui'), { recursive: true });
    // BUDO_MPS_MAIN runs another main.ts (e.g. an older version, to see a test fail).
    const mainPath = process.env.BUDO_MPS_MAIN || join(here, 'main.ts');
    const stripped = execFileSync(stripper, [mainPath], { encoding: 'utf8' });
    const exports = '\nexport const __test = { ' + INTERNALS.join(', ') +
        ', state: () => ({ selectedSongId, selectedPresetId, outputHandle, inputHandle, sequencePlaying, presetSequences, livePresets, eventLog }) };\n';
    writeFileSync(join(directory, 'main.mjs'), stripped + exports);
    const { __test: app } = await import(pathToFileURL(join(directory, 'main.mjs')).href);

    let clock = 1000;
    const frame = (advance = 16) => { clock += advance; frameCallback(clock); };
    const send = message => midi.onInput({ timestamp: 0, type: message.status & 0xF0, channel: message.status & 0x0F, data1: 0, data2: 0, ...message });
    const sent = () => log.filter(entry => entry[0] === 'send').map(entry => entry[1]);
    const clearLog = () => { log.length = 0; };
    frame();
    return { app, midi, frame, send, sent, log, clearLog };
}

const cc = (controller, value, channel = 0) => ({ status: 0xB0 + channel, data1: controller, data2: value });
const note = (on, key, velocity = 100, channel = 0) => ({ status: (on ? 0x90 : 0x80) + channel, data1: key, data2: on ? velocity : 0 });
const presetPayload = (app, id) => app.decodePreset(String(app.state().livePresets.find(row => row.id === id).payload));

test('a Bank Select and Program Change burst is stored in arrival order', async () => {
    const { app, send, frame } = await launch();
    // Program Change was first touched long ago...
    send({ status: 0xC0, data1: 1 });
    frame();
    // ...then the synth sends bank and program in one burst (same millisecond).
    const realNow = Date.now;
    Date.now = () => 5_000_000;
    try {
        send(cc(0, 2));
        send(cc(32, 0));
        send({ status: 0xC0, data1: 7 });
    } finally {
        Date.now = realNow;
    }
    app.createPreset('Lead');
    const commands = presetPayload(app, app.state().selectedPresetId);
    assert.deepEqual(commands.map(command => command.bytes), [[0xB0, 0, 2], [0xB0, 32, 0], [0xC0, 7]]);
    // receivedAt stays a non-negative number (the published schema).
    assert.ok(commands.every(command => typeof command.receivedAt === 'number' && command.receivedAt >= 0));
});

test('pedals, channel-mode controllers, and pressure are not stored as preset state', async () => {
    const { app, send } = await launch();
    send(cc(7, 100));               // volume: state
    send(cc(64, 127));              // sustain: a gesture
    send(cc(122, 0));               // local control off: never replay
    send(cc(123, 0));               // all notes off
    send({ status: 0xD0, data1: 50 });              // channel pressure
    send({ status: 0xA0, data1: 60, data2: 40 });   // poly pressure
    app.createPreset('Pad');
    assert.deepEqual(presetPayload(app, app.state().selectedPresetId).map(command => command.bytes), [[0xB0, 7, 100]]);
});

test('notes and pedals still held at Stop are released in the memo', async () => {
    const { app, send, frame } = await launch();
    app.createPreset('Keys');
    app.startSequenceRecording();
    send(note(true, 60));
    frame(100);
    send(note(true, 64));
    send(cc(64, 127));              // sustain down: recorded in the memo
    frame(100);
    send(note(false, 60));
    app.stopSequenceRecording();
    const memo = app.state().presetSequences[0];
    const events = app.decodeNoteSequence(String(memo.payload));
    const last = events.slice(-2).map(event => event.bytes);
    assert.deepEqual(last.sort(), [[0x80, 64, 0], [0xB0, 64, 0]].sort());
    assert.ok(events.some(event => event.bytes[0] === 0xB0 && event.bytes[2] === 127), 'the pedal is recorded');
});

test('past the event limit, releases are still recorded', async () => {
    const { app, send } = await launch();
    app.createPreset('Long');
    app.startSequenceRecording();
    send(note(true, 40));
    for (let index = 0; index < 300; index++) send(note(index % 2 === 0, 50));
    send(note(false, 40));          // past the limit: still recorded
    app.stopSequenceRecording();
    const events = app.decodeNoteSequence(String(app.state().presetSequences[0].payload));
    const ons = new Set(), offs = new Set();
    for (const event of events) {
        if (event.bytes[0] === 0x90 && event.bytes[2] > 0) ons.add(event.bytes[1]);
        if (event.bytes[0] === 0x80) offs.add(event.bytes[1]);
    }
    for (const key of ons) assert.ok(offs.has(key), 'note ' + key + ' is released');
});

test('stopping playback early releases the notes it left sounding', async () => {
    const { app, frame, sent, clearLog } = await launch();
    app.createPreset('Memo');
    const presetId = app.state().selectedPresetId;
    app.savePresetSequence(presetId, [
        { timeMs: 0, label: 'on', bytes: [0x90, 60, 100] },
        { timeMs: 0, label: 'pedal', bytes: [0xB0, 64, 127] },
        { timeMs: 2000, label: 'off', bytes: [0x80, 60, 0] },
    ]);
    app.playPresetSequence(app.state().presetSequences[0].id);
    clearLog();
    frame();                        // the note and the pedal go down
    assert.deepEqual(sent(), [[0x90, 60, 100], [0xB0, 64, 127]]);
    app.stopSequencePlayback();     // e.g. switching preset
    assert.deepEqual(sent().slice(2).sort(), [[0x80, 60, 0], [0xB0, 64, 0]].sort());
});

test('a lengthened short note ends before the same key is struck again', async () => {
    const { app } = await launch();
    const prepared = app.prepareSequencePlaybackEvents([
        { timeMs: 0, label: '', bytes: [0x90, 60, 100] },
        { timeMs: 10, label: '', bytes: [0x80, 60, 0] },
        { timeMs: 30, label: '', bytes: [0x90, 60, 100] },
        { timeMs: 200, label: '', bytes: [0x80, 60, 0] },
    ]);
    assert.deepEqual(prepared.map(event => [event.timeMs, event.bytes[0]]), [[0, 0x90], [29, 0x80], [30, 0x90], [200, 0x80]]);
    // An isolated short note still gets the minimum length.
    const single = app.prepareSequencePlaybackEvents([
        { timeMs: 0, label: '', bytes: [0x90, 62, 100] },
        { timeMs: 5, label: '', bytes: [0x80, 62, 0] },
    ]);
    assert.equal(single[1].timeMs, 45);
});

test('old memos that end with a note on are closed at playback', async () => {
    const { app } = await launch();
    const prepared = app.prepareSequencePlaybackEvents([
        { timeMs: 0, label: '', bytes: [0x91, 60, 100] },
        { timeMs: 500, label: '', bytes: [0x91, 62, 100] },
        { timeMs: 600, label: '', bytes: [0x81, 62, 0] },
    ]);
    const last = prepared[prepared.length - 1];
    assert.deepEqual(last.bytes, [0x81, 60, 0]);
    assert.ok(last.timeMs >= 600);
});

test('playback after a stall resumes instead of bursting', async () => {
    const { app, frame, sent, clearLog } = await launch();
    app.createPreset('Memo');
    app.savePresetSequence(app.state().selectedPresetId, [
        { timeMs: 0, label: '', bytes: [0x90, 60, 100] },
        { timeMs: 100, label: '', bytes: [0x80, 60, 0] },
        { timeMs: 200, label: '', bytes: [0x90, 62, 100] },
        { timeMs: 300, label: '', bytes: [0x80, 62, 0] },
    ]);
    app.playPresetSequence(app.state().presetSequences[0].id);
    clearLog();
    frame(0);
    frame(5000);                    // a five-second stall
    assert.equal(sent().length, 2, 'one overdue event, not all of them: ' + JSON.stringify(sent()));
    frame(120);
    assert.equal(sent().length, 3);
});

test('a recall sends channel messages back to back and paces SysEx', async () => {
    const { app, frame, sent, clearLog, send } = await launch();
    send({ status: 0xF0, data: [0xF0, 0x43, 0x10, 0xF7] });
    send(cc(7, 90));
    send({ status: 0xF0, data: [0xF0, 0x43, 0x11, 0xF7] });
    send(cc(10, 64));
    app.createPreset('Dump');
    clearLog();
    app.recallPreset(app.state().selectedPresetId);
    assert.deepEqual(sent(), [[0xF0, 0x43, 0x10, 0xF7]]);
    frame(10);
    assert.equal(sent().length, 1);
    frame(15);                      // 25 ms later: the next batch
    assert.deepEqual(sent().slice(1), [[0xB0, 7, 90], [0xF0, 0x43, 0x11, 0xF7]]);
    frame(25);
    assert.deepEqual(sent().slice(3), [[0xB0, 10, 64]]);
});

test('a Live recall sends MIDI before any database write', async () => {
    const { app, send, log, clearLog } = await launch();
    send(cc(7, 90));
    app.createPreset('Song A');
    const { selectedSongId, selectedPresetId } = app.state();
    clearLog();
    app.recallLivePreset(selectedSongId, selectedPresetId);
    assert.equal(log[0][0], 'send', 'first operation: ' + JSON.stringify(log[0]));
    assert.ok(log.some(entry => entry[0] === 'db'), 'the selection is still saved');
});

test('the chosen output is found by name and never replaced by another device', async () => {
    const { app, midi } = await launch({ outputs: ['Synth A', 'Synth B'] });
    app.chooseOutput(1);
    assert.equal(midi.output, 'Synth B');
    // Synth A unplugged: the list shifts; the output must stay Synth B.
    midi.outputs = [{ id: 0, name: 'Synth B' }];
    midi.onChange({ inputs: midi.inputs, outputs: midi.outputs, generation: 1 });
    assert.equal(midi.output, 'Synth B');
    // Synth B unplugged: no fallback to whatever is left.
    midi.outputs = [{ id: 0, name: 'Synth A' }];
    midi.onChange({ inputs: midi.inputs, outputs: midi.outputs, generation: 2 });
    assert.equal(midi.output, null);
    assert.equal(app.state().outputHandle, -1);
    // Back again: reconnected.
    midi.outputs = [{ id: 0, name: 'Synth A' }, { id: 1, name: 'Synth B' }];
    midi.onChange({ inputs: midi.inputs, outputs: midi.outputs, generation: 3 });
    assert.equal(midi.output, 'Synth B');
});

test('exports round-trip unchanged, and large exports are accepted', async () => {
    const { app, send } = await launch();
    send(cc(7, 90));
    send({ status: 0xF0, data: [0xF0, 0x7E, 0x01, 0xF7] });
    app.createPreset('Keep');
    const document = app.buildDatabaseExport();
    const text = JSON.stringify(document, null, 2);
    assert.deepEqual(app.validateDatabaseExport(text), JSON.parse(text));
    // Over the old 8 MiB limit, under the new 64 MiB one.
    const padded = JSON.stringify({ ...document, padding: undefined }) + ' '.repeat(9 * 1024 * 1024);
    assert.doesNotThrow(() => app.validateDatabaseExport(padded));
});
