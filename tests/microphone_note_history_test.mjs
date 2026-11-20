// Run with: node tests/microphone_note_history_test.mjs
import assert from 'node:assert/strict';

let receiveSamples;
let nextFrame;
let frequency = 0;
let width = 800;
let calls = [];
let pointer = { pressed: false, x: 0, y: 0 };
let textureCount = 0;
let textureWidth = 0;
let outputCallback;
let outputOpens = 0;
let escapePressed = false;
const closedOutputs = [];
const record = name => (...args) => calls.push([name, ...args]);
globalThis.sys = {
    audio: {
        getSampleRate: () => 48000,
        openInput: (options, callback) => { receiveSamples = callback; return 1; },
        openOutput: (options, callback) => {
            assert.equal(options.channels, 1);
            outputCallback = callback;
            return ++outputOpens;
        },
        closeOutput: stream => closedOutputs.push(stream),
        getSpectrum: (samples, db) => db.fill(-90),
        detectPitch: () => ({ frequency, clarity: frequency ? 1 : 0 }),
    },
    window: { getWidth: () => width, getHeight: () => 1800, getDisplayDensity: () => 2 },
    canvas: new Proxy({
        measureText: (text, size) => text.length * size / 2,
    }, { get: (target, key) => target[key] || record(key) }),
    gl: new Proxy({
        createTexture2D: (w, h, format, pixels) => {
            textureCount++;
            textureWidth = w;
            assert.equal(pixels.length, w * h);
            return 1;
        },
        createProgram: () => 2,
    }, { get: (target, key) => target[key] || record(key) }),
    input: {
        get: () => ({ pointer, focused: true, totalTime: 0, deltaTime: 1 / 60 }),
        isKeyPressed: key => key === 41 && escapePressed,
    },
    animation: { requestFrame: callback => { nextFrame = callback; } },
    log: record('log'),
};

await import('../examples/20_microphone/main.js');
const samples = new Float32Array(1024);
function columns(hz, count) {
    frequency = hz;
    samples.fill(hz ? 0.5 : 0);
    for (let i = 0; i < count; i++) receiveSamples(samples, { sampleRate: 48000, latency: 0, overruns: 0 });
}
function frame() {
    calls = [];
    nextFrame();
    return calls.filter(call => call[0] === 'drawText' && /^[A-G]#?$/.test(call[1]));
}
function clickText(label) {
    const option = calls.findLast(call => call[0] === 'drawText' && call[1] === label);
    assert.ok(option, `option ${label} is visible`);
    pointer = { pressed: true, x: option[2] * 2, y: (option[3] - 5) * 2 };
    frame();
    pointer = { pressed: false, x: 0, y: 0 };
}
function chooseOption(label) {
    frame();
    if (['Auto', 'Off', '128', '256', '512', '1k', '2k', '4k'].includes(label)) clickText('FFT');
    clickText(label);
}
frame();
assert.ok(!calls.some(call => call[0] === 'drawText' && call[1] === 'Off'), 'FFT choices are hidden until opened');
chooseOption('FFT');
for (const label of ['Auto', 'Off', '128', '256', '512', '1k', '2k', '4k'])
    assert.ok(calls.some(call => call[0] === 'drawText' && call[1] === label), `dropdown offers ${label}`);
const menu = calls.findLast(call => call[0] === 'drawRoundRect' && call[4] > 48);
const fftHeading = calls.find(call => call[0] === 'drawText' && call[1] === 'FFT');
assert.ok(menu[2] >= 0 && menu[2] + menu[4] < fftHeading[3] - 15, 'FFT menu opens upward within the window');
escapePressed = true;
frame();
escapePressed = false;
assert.ok(!calls.some(call => call[0] === 'drawText' && call[1] === 'Off'), 'Escape dismisses the dropdown');
chooseOption('FFT');
chooseOption('Monitor');
assert.equal(outputOpens, 0, 'dismissing the menu does not click through to Monitor');
assert.ok(!calls.some(call => call[0] === 'drawText' && call[1] === 'Off'), 'outside presses dismiss the dropdown');
chooseOption('2k'); // Keep Auto from changing the analysis size during long histories.
assert.ok(!calls.some(call => call[0] === 'drawText' && call[1] === 'Off'), 'choosing an FFT size closes the dropdown');
assert.equal(outputOpens, 0, 'monitoring starts off');
assert.ok(calls.some(call => call[0] === 'drawText' && call[1] === 'Monitor'), 'Monitor remains available');
assert.ok(!calls.some(call => call[0] === 'drawText' && /^(Record|Play|Save)/.test(call[1])),
    'recording, playback, saving, and their timeline are removed');

columns(440, 16);
let labels = frame();
assert.deepEqual(labels.map(call => call[1]), ['A'], 'a sustained note gets one label');
const initialX = labels[0][2];
const initialY = labels[0][3];
const clip = calls.find(call => call[0] === 'clipRect');
const timeHeading = calls.find(call => call[0] === 'drawText' && call[1] === 'Time');
assert.ok(clip[2] + clip[4] < timeHeading[3] - 22, 'time controls sit below the graphs');
const step = clip[3] / 192;
assert.ok(initialX >= clip[1] && initialX + 6 <= clip[1] + clip[3], 'newest label stays inside the graph');
assert.ok(initialY >= clip[2] && initialY <= clip[2] + clip[4], 'label stays inside the frequency bounds');
assert.ok(calls.findIndex(call => call[0] === 'clipRect') < calls.findIndex(call => call[0] === 'clear' && call[1] === '#00000000'),
    'transparent clearing is clipped to the spectrogram');
assert.ok(calls.findIndex(call => call === labels[0]) < calls.findIndex(call => call[0] === 'drawRegionImmediate'),
    'labels are ready for shader compositing');

columns(0, 24);
labels = frame();
assert.deepEqual(labels.map(call => call[1]), ['A'], 'silence does not add held-pitch labels');
// The initial label was clamped against the right edge; subsequent scrolling is exact.
const scrolledX = labels[0][2];
columns(0, 5);
labels = frame();
assert.ok(Math.abs(labels[0][2] - (scrolledX - 5 * step)) < 1e-6, 'history moves by the spectrogram column spacing');
assert.ok(labels[0][2] < initialX, 'past notes scroll left');

columns(440, 16);
labels = frame();
assert.deepEqual(labels.map(call => call[1]), ['A', 'A'], 'the same note after silence gets a new label');
columns(415.3047, 16);
labels = frame();
assert.deepEqual(labels.map(call => call[1]), ['A', 'A', 'G#'], 'note changes include accidental letters without octaves');
assert.ok(labels[2][3] > labels[1][3], 'lower fundamentals appear lower on the log-frequency axis');

columns(0, 192);
assert.deepEqual(frame(), [], 'notes outside the default span are hidden');
columns(523.251, 16);
assert.deepEqual(frame().map(call => call[1]), ['C'], 'new labels work after the visible window advances');
width = 1200;
labels = frame();
const resizedClip = calls.find(call => call[0] === 'clipRect');
assert.ok(labels[0][2] >= resizedClip[1] && labels[0][2] + 6 <= resizedClip[1] + resizedClip[3],
    'history follows resized graph geometry at high display density');

chooseOption('Off');
columns(523.251, 24);
assert.deepEqual(frame(), [], 'FFT off hides spectrogram labels');
chooseOption('2k');
columns(523.251, 16);
assert.deepEqual(frame().map(call => call[1]), ['C', 'C'], 'FFT-off columns are gaps, not stale pitches');

const capacity = Math.round(30 * 48000 / 1024);
columns(0, capacity);
assert.deepEqual(frame(), [], 'notes expire after the retained history wraps');
assert.ok(calls.some(call => call[0] === 'updateTexture2D' && call[4] === capacity && call[5] === 128),
    'a burst spanning the ring uploads the full texture');
assert.equal(textureWidth, capacity, 'the texture retains the longest selectable span');

columns(440, 8);
columns(0, 194);
assert.deepEqual(frame(), [], 'a note older than the default span is hidden');
let previousX = 0;
for (const seconds of [5, 10, 30]) {
    chooseOption(seconds + 's');
    labels = frame();
    assert.deepEqual(labels.map(call => call[1]), ['A'], `${seconds}s reveals retained notes without new audio`);
    assert.ok(labels[0][2] > previousX, 'longer spans compress the history toward the right');
    previousX = labels[0][2];
    const visible = calls.find(call => call[0] === 'setUniform1f' && call[2] === 'u_visibleColumns')[3];
    assert.ok(Math.abs(visible * 1024 / 48000 - seconds) <= 1024 / 48000 / 2,
        'the selected duration is accurate to half an analysis column');
    const graph = calls.find(call => call[0] === 'clipRect');
    const waveLines = calls.filter(call => call[0] === 'drawLine' && call[1] === call[3]
        && call[2] >= 314 && call[4] <= graph[2] - 12 && call[4] - call[2] > 1);
    assert.equal(waveLines.length, 8, 'the waveform retains the same eight signal columns');
    const waveCenter = waveLines.reduce((sum, call) => sum + call[1], 0) / waveLines.length;
    assert.ok(Math.abs(waveCenter - (labels[0][2] + 3)) < 1e-6,
        'waveform and note labels share the selected time scale');
}
chooseOption('4.1s');
assert.deepEqual(frame(), [], 'the original time span remains selectable');
chooseOption('30s');
assert.deepEqual(frame().map(call => call[1]), ['A'], 'shrinking then expanding preserves history');
columns(0, capacity);
assert.deepEqual(frame(), [], 'expired notes do not return in the longest span');
assert.equal(textureCount, 1, 'span changes reuse the existing texture');

chooseOption('Off');
chooseOption('10s');
chooseOption('2k');
frame();
assert.equal(calls.find(call => call[0] === 'setUniform1f' && call[2] === 'u_visibleColumns')[3],
    Math.round(10 * 48000 / 1024), 'time selection also works while FFT is off');

chooseOption('Monitor');
assert.equal(outputOpens, 1, 'Monitor opens one live output stream');
columns(440, 1);
const monitored = new Float32Array(512);
outputCallback(monitored, { latency: 0.025 });
assert.ok(monitored.every(sample => sample === 0.5), 'Monitor forwards live microphone samples');
frame();
assert.ok(calls.some(call => call[0] === 'drawText' && call[1].includes('monitor +25 ms')),
    'live monitoring latency remains visible');
chooseOption('Monitor');
assert.deepEqual(closedOutputs, [1], 'toggling Monitor off closes its stream');
frame();
assert.ok(!calls.some(call => call[0] === 'drawText' && call[1].includes('monitor +')),
    'monitor latency disappears when monitoring is off');
columns(0, 1);
chooseOption('Monitor');
assert.equal(outputOpens, 2, 'Monitor can be enabled again');
const empty = new Float32Array(128);
outputCallback(empty, { latency: 0.025 });
assert.ok(empty.every(sample => sample === 0), 'restarting Monitor discards samples from the previous stream');
chooseOption('Monitor');
assert.deepEqual(closedOutputs, [1, 2], 'the restarted stream also closes cleanly');
for (const logicalWidth of [320, 360, 400, 800]) {
    width = logicalWidth * 2;
    frame();
    const toolbarY = 900 - 16 - 48;
    const panels = calls.filter(call => call[0] === 'drawRoundRect' && call[2] === toolbarY && call[4] === 48)
        .sort((a, b) => a[1] - b[1]);
    assert.equal(panels.length, 3, 'Time, FFT, and Monitor share one bottom row');
    assert.ok(panels[0][1] >= 16 && panels[2][1] + panels[2][3] <= logicalWidth - 16, 'toolbar stays within screen margins');
    for (let i = 1; i < panels.length; i++)
        assert.ok(panels[i - 1][1] + panels[i - 1][3] < panels[i][1], 'toolbar groups do not overlap');
    for (const text of calls.filter(call => call[0] === 'drawText' && call[3] > toolbarY && call[1] !== 'Time')) {
        assert.ok(panels.some(panel => text[2] >= panel[1] && text[2] + text[1].length * text[4] / 2 <= panel[1] + panel[3]),
            `toolbar label ${text[1]} fits at ${logicalWidth}px`);
    }
    chooseOption('FFT');
    const openMenu = calls.findLast(call => call[0] === 'drawRoundRect' && call[4] > 48);
    assert.ok(openMenu[1] >= 16 && openMenu[1] + openMenu[3] <= logicalWidth - 16, 'dropdown remains in bounds after resize');
    const lastNote = calls.findLastIndex(call => call[0] === 'drawText' && /^[A-G]#?$/.test(call[1]));
    assert.ok(calls.indexOf(openMenu) > lastNote, 'dropdown is drawn above the graph labels for shader compositing');
    chooseOption('FFT');
}
console.log('microphone_note_history_test: ok');
