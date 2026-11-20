/**
 * Microphone
 *
 * Analyzes the microphone with an input stream (sys.audio.openInput):
 * - a level meter and a scrolling waveform of what the microphone hears;
 * - Monitor: plays the microphone live (use headphones), to hear the latency;
 * - a spectrogram (a native FFT per column, drawn by a shader), the pitch (by
 *   autocorrelation) with its nearest note and a tuner needle, and the
 *   loudest frequency. Note letters on the spectrogram keep the recent
 *   sequence of fundamentals, one label per continuous note.
 *   The FFT size is an option: Auto (the default) picks
 *   the largest size the device analyzes without slowing down; Off skips the
 *   analysis.
 *
 * The waveform and the spectrogram share their columns: 1024 samples each
 * (about 21 ms), the newest on the right, so they line up. Time selects
 * the original 192-column span or 5, 10, or 30 seconds of retained history.
 *
 * On Android, app.json lists the RECORD_AUDIO permission: the first
 * openInput asks for it and fails, so the app opens the microphone again
 * when it gets the focus back from the permission dialog (or on a tap).
 */

import { SpectrumAnalyzer, ROWS, SIZES, noteOf } from './spectrum.js';

const SAMPLE_RATE = sys.audio.getSampleRate();

// Laid out in density-independent pixels, like budo-ui.
const UI_SCALE = Math.max(1, sys.window.getDisplayDensity());

const COLORS = {
    background: '#0f1422',
    panel: '#182036',
    text: '#e8eef7',
    dim: '#8a96b0',
    accent: '#5eead4',
    danger: '#f87171',
    monitor: '#fbbf24',
    button: '#25304d',
};

// ============ Microphone ============

let mic = -1;
let micError = '';
let micInfo = null; // the last chunk's info: latency, overruns, ...

let level = 0; // smoothed peak, 0..1
let peakHold = 0;
let peakHoldTime = 0;

// Columns: the waveform's min/max and the spectrogram's spectrum, every
// COLUMN_SAMPLES samples, retaining up to 30 s regardless of the visible span.
const DEFAULT_COLUMNS = 192;
const COLUMN_SAMPLES = 1024;
const TIME_SPANS = [DEFAULT_COLUMNS * COLUMN_SAMPLES / SAMPLE_RATE, 5, 10, 30];
const COLUMNS = Math.max(DEFAULT_COLUMNS, Math.round(30 * SAMPLE_RATE / COLUMN_SAMPLES));
let timeSpan = TIME_SPANS[0];
let visibleColumns = DEFAULT_COLUMNS;
const waveMin = new Float32Array(COLUMNS);
const waveMax = new Float32Array(COLUMNS);
let columnMin = 0, columnMax = 0, columnFill = 0;
let columnCount = 0; // columns ever completed

// The spectrogram: one analyzer spectrum per column, uploaded to a
// COLUMNS x ROWS texture at the next frame.
const spectrogram = new Uint8Array(COLUMNS * ROWS);
const pitchHistory = new Float32Array(COLUMNS);
let spectrumUploaded = 0;
let spectrumTexture = -1;
let spectrumShader = -1;
let pitch = 0; // the displayed pitch: held a moment after the sound stops
let pitchTime = -10;

// ============ FFT size ============

// 'auto', 'off', or one of SIZES.
let fftChoice = 'auto';
let autoSize = 2048; // Auto's current size
let analyzer = new SpectrumAnalyzer(SAMPLE_RATE, autoSize);
let analysisMs = 0; // average analysis time, for Auto
let analysisSamples = 0;
let autoChangedAt = 0;
const clock = () => (globalThis.performance ? performance.now() : Date.now());

// Auto keeps the analysis under this share of the real time: columns come
// every ~21 ms, so 25% is about 5 ms per analysis, leaving the frames fluid.
const AUTO_BUDGET_MS = 0.25 * COLUMN_SAMPLES / SAMPLE_RATE * 1000;

function fftSize() {
    return fftChoice === 'auto' ? autoSize : fftChoice === 'off' ? 0 : fftChoice;
}

function setAnalyzerSize(size) {
    analyzer = size ? new SpectrumAnalyzer(SAMPLE_RATE, size) : null;
    analysisMs = 0;
    analysisSamples = 0;
    pitchTime = -10;
}

function chooseFft(choice) {
    fftChoice = choice;
    setAnalyzerSize(fftSize());
}

// Auto: step down when the analysis is over budget; step up when the next
// size (a bit more than twice the work) would fit comfortably. Waits for a
// few measurements, and a second after each change.
function adjustAuto(now) {
    if (fftChoice !== 'auto' || analysisSamples < 20 || now - autoChangedAt < 1) return;
    const index = SIZES.indexOf(autoSize);
    let next = autoSize;
    if (analysisMs > AUTO_BUDGET_MS && index > 0) next = SIZES[index - 1];
    else if (analysisMs * 2.2 < AUTO_BUDGET_MS * 0.8 && index < SIZES.length - 1) next = SIZES[index + 1];
    if (next === autoSize) return;
    sys.log(`FFT auto: ${autoSize} -> ${next} (analysis ${analysisMs.toFixed(2)} ms, budget ${AUTO_BUDGET_MS.toFixed(2)} ms)`);
    autoSize = next;
    autoChangedAt = now;
    setAnalyzerSize(autoSize);
}

// A column is complete: store the waveform's min/max, and analyze.
function finishColumn() {
    const at = columnCount % COLUMNS;
    waveMin[at] = columnMin;
    waveMax[at] = columnMax;
    columnMin = columnMax = 0;
    if (analyzer) {
        const start = clock();
        analyzer.analyze();
        const spent = clock() - start;
        analysisMs = analysisSamples === 0 ? spent : analysisMs * 0.9 + spent * 0.1;
        analysisSamples++;
        spectrogram.set(analyzer.rows, at * ROWS);
        pitchHistory[at] = analyzer.pitch;
    } else {
        spectrogram.fill(0, at * ROWS, (at + 1) * ROWS);
        pitchHistory[at] = 0;
    }
    columnCount++;
}

// Live monitoring: captured samples wait here for the output stream.
const monitorQueue = new Float32Array(SAMPLE_RATE);
let monitorWritten = 0;
let monitorRead = 0;
let monitorStream = -1;
let monitorLatency = 0; // the monitor's output queue, in seconds

function openMic() {
    if (mic >= 0) return;
    mic = sys.audio.openInput({ channels: 1 }, onMicSamples);
    micError = mic < 0 ? sys.audio.getError() : '';
    if (mic >= 0) sys.log(`Microphone open at ${SAMPLE_RATE} Hz`);
}

function onMicSamples(samples, info) {
    micInfo = info;
    let min = 0, max = 0;
    for (let i = 0; i < samples.length; i++) {
        const s = samples[i];
        if (s < min) min = s;
        if (s > max) max = s;
    }
    level = Math.max(max, -min, level * 0.92);
    if (min < columnMin) columnMin = min;
    if (max > columnMax) columnMax = max;
    if (analyzer) analyzer.write(samples);
    columnFill += samples.length;
    if (columnFill >= COLUMN_SAMPLES) {
        columnFill -= COLUMN_SAMPLES;
        finishColumn();
    }

    if (monitorStream >= 0) {
        for (let i = 0; i < samples.length; i++) {
            monitorQueue[monitorWritten % monitorQueue.length] = samples[i];
            monitorWritten++;
        }
    }
}

// ============ Monitoring ============

function toggleMonitor() {
    if (monitorStream >= 0) {
        sys.audio.closeOutput(monitorStream);
        monitorStream = -1;
        return;
    }
    monitorWritten = monitorRead = 0;
    // An adaptive queue: as small as this device allows (info.latency).
    monitorStream = sys.audio.openOutput({ channels: 1 }, (buffer, info) => {
        monitorLatency = info.latency;
        // Never let the queue grow: skip ahead if the input got ahead of us.
        const waiting = monitorWritten - monitorRead;
        if (waiting > buffer.length * 3) monitorRead = monitorWritten - buffer.length;
        const count = Math.min(buffer.length, monitorWritten - monitorRead);
        for (let i = 0; i < count; i++)
            buffer[i] = monitorQueue[(monitorRead + i) % monitorQueue.length];
        monitorRead += count;
    });
}

// ============ Interface ============

let buttons = [];
let fftButton = null;
let fftMenuOpen = false;

function layoutToolbar(width, height) {
    const pad = 16;
    const gap = 8;
    const h = 48;
    const top = height - pad - h;
    const contentW = width - pad * 2;
    const monitorW = Math.min(100, contentW * 0.24);
    const fftW = Math.min(112, contentW * 0.26);
    const timeW = contentW - monitorW - fftW - gap * 2;
    fftButton = { x: pad + timeW + gap, y: top, w: fftW, h };
    buttons = [{
        label: 'Monitor', color: COLORS.monitor, active: monitorStream >= 0,
        enabled: mic >= 0, action: toggleMonitor,
        x: width - pad - monitorW, y: top, w: monitorW, h,
    }];
    const labelW = width >= 380 ? 32 : 0;
    const timeSegment = (timeW - labelW) / TIME_SPANS.length;
    timeOptions = TIME_SPANS.map((choice, i) => ({
        choice,
        label: (i === 0 ? choice.toFixed(1) : String(choice)) + 's',
        x: pad + labelW + i * timeSegment, y: top, w: timeSegment, h,
    }));
    const choices = ['auto', 'off', ...SIZES];
    const rowH = Math.min(36, (top - pad - gap) / choices.length);
    const menuW = Math.max(112, fftW);
    options = choices.map((choice, i) => ({
        choice, label: fftLabel(choice),
        x: Math.min(fftButton.x, width - pad - menuW),
        y: top - gap - (choices.length - i) * rowH, w: menuW, h: rowH,
    }));
    return top;
}

function inside(rect, x, y) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

function drawButton(b) {
    const canvas = sys.canvas;
    // Disabled buttons are faded: a "#RRGGBBAA" color (setFillColor resets the alpha).
    const fade = b.enabled ? '' : '66';
    canvas.setFillColor((b.active ? b.color : COLORS.button) + fade);
    canvas.drawRoundRect(b.x, b.y, b.w, b.h, 12, 12);
    canvas.setFillColor((b.active ? COLORS.background : COLORS.text) + fade);
    // The label is centered, shifted right to make room for the dot.
    const size = b.w < 110 ? 14 : 20;
    const dot = b.active ? 0 : 14;
    const tw = canvas.measureText(b.label, size);
    const tx = b.x + (b.w - tw + dot) / 2;
    canvas.drawText(b.label, tx, b.y + b.h / 2 + size * 0.35, size);
    if (!b.active) {
        canvas.setFillColor(b.color + fade);
        canvas.drawCircle(tx - 10, b.y + b.h / 2, 4);
    }
}

function toDb(value) {
    return value > 1e-5 ? 20 * Math.log10(value) : -100;
}

function drawMeter(x, y, w, h) {
    const canvas = sys.canvas;
    canvas.setFillColor(COLORS.panel);
    canvas.drawRoundRect(x, y, w, h, 8, 8);
    // -60 dB to 0 dB.
    const fraction = (db) => Math.max(0, Math.min(1, (db + 60) / 60));
    const db = toDb(level);
    const filled = fraction(db) * w;
    canvas.setFillColor(db > -3 ? COLORS.danger : db > -12 ? COLORS.monitor : COLORS.accent);
    canvas.drawRoundRect(x, y, Math.max(filled, 0.1), h, 8, 8);
    const hold = x + fraction(toDb(peakHold)) * w;
    canvas.setStrokeColor(COLORS.text);
    canvas.setStrokeWidth(2);
    canvas.drawLine(hold, y, hold, y + h);
    canvas.setFillColor(COLORS.dim);
    for (const mark of [-48, -36, -24, -12, -6]) {
        const mx = x + fraction(mark) * w;
        canvas.drawText(String(mark), mx - 10, y + h + 16, 12);
    }
    canvas.setFillColor(COLORS.text);
    canvas.drawText(db > -100 ? db.toFixed(0) + ' dB' : '-∞ dB', x + w - 64, y - 8, 16);
}

function drawWaveform(x, y, w, h) {
    const canvas = sys.canvas;
    canvas.setFillColor(COLORS.panel);
    canvas.drawRoundRect(x, y, w, h, 8, 8);
    const mid = y + h / 2;
    canvas.setStrokeColor('#2c3858');
    canvas.setStrokeWidth(1);
    canvas.drawLine(x, mid, x + w, mid);
    // One line per column, the newest on the right: the same columns, at
    // the same places, as the spectrogram below.
    const step = w / visibleColumns;
    canvas.setStrokeColor(COLORS.accent);
    canvas.setStrokeWidth(Math.max(1, step * 0.75));
    for (let c = 0; c < visibleColumns; c++) {
        const column = columnCount - visibleColumns + c;
        if (column < 0) continue;
        const at = column % COLUMNS;
        const top = mid - waveMax[at] * h * 0.48;
        const bottom = mid - waveMin[at] * h * 0.48;
        const cx = x + (c + 0.5) * step;
        canvas.drawLine(cx, top, cx, Math.max(bottom, top + 1));
    }
}

function drawPitch(x, y, w, now) {
    const canvas = sys.canvas;
    if (!analyzer) {
        canvas.setFillColor(COLORS.dim);
        canvas.drawText('FFT off: no pitch', x, y, 18);
        return;
    }
    if (analyzer.pitch > 0) {
        pitch = analyzer.pitch;
        pitchTime = now;
    }
    // The loudest frequency, which is often a harmonic of the pitch.
    if (analyzer.dominant > 0) {
        const loudest = 'loudest ' + Math.round(analyzer.dominant) + ' Hz';
        canvas.setFillColor(COLORS.dim);
        canvas.drawText(loudest, x + w - canvas.measureText(loudest, 13), y, 13);
    }
    const age = now - pitchTime;
    if (age > 1) {
        canvas.setFillColor(COLORS.dim);
        canvas.drawText(`Sing or whistle a note (from ${Math.round(analyzer.lowestPitch)} Hz)`, x, y, 18);
        return;
    }
    const note = noteOf(pitch);
    const fade = age > 0.25 ? '80' : '';
    canvas.setFillColor(COLORS.text + fade);
    const hz = pitch.toFixed(1) + ' Hz';
    canvas.drawText(hz, x, y, 26);
    const noteX = x + canvas.measureText(hz, 26) + 18;
    canvas.setFillColor(COLORS.accent + fade);
    canvas.drawText(note.name, noteX, y, 26);
    // How far from the note, -50 to +50 cents.
    const cents = (note.cents > 0 ? '+' : '') + note.cents + '¢';
    const centsX = noteX + canvas.measureText(note.name, 26) + 10;
    canvas.setFillColor((Math.abs(note.cents) <= 10 ? COLORS.accent : COLORS.monitor) + fade);
    canvas.drawText(cents, centsX, y, 16);
}

// ============ Tuner ============

const TUNER_RANGE = 50; // cents each side
const TUNER_DEGREES = 60; // the needle's swing each side of vertical
const IN_TUNE_CENTS = 5;
let needleCents = 0;

// An arc gauge: the needle shows how many cents the pitch is from its nearest
// note. It eases toward the reading, and back to the center without a note.
function drawTuner(x, y, w, h, now, dt) {
    const canvas = sys.canvas;
    const active = now - pitchTime < 1;
    const target = active ? noteOf(pitch).cents : 0;
    needleCents += (target - needleCents) * (1 - Math.exp(-dt * (active ? 14 : 4)));

    const radius = Math.min(h - 14, (w / 2) / Math.sin(TUNER_DEGREES * Math.PI / 180) - 12);
    const cx = x + w / 2, cy = y + h - 4;
    const angle = (cents) => (-90 + cents / TUNER_RANGE * TUNER_DEGREES) * Math.PI / 180;
    const point = (cents, r) => [cx + r * Math.cos(angle(cents)), cy + r * Math.sin(angle(cents))];
    const arc = (r, from, to) => canvas.drawArc(cx - r, cy - r, 2 * r, 2 * r,
        -90 + from / TUNER_RANGE * TUNER_DEGREES, (to - from) / TUNER_RANGE * TUNER_DEGREES, false);

    canvas.setStrokeCap('round');
    canvas.setStrokeColor(COLORS.panel);
    canvas.setStrokeWidth(10);
    arc(radius, -TUNER_RANGE, TUNER_RANGE);
    canvas.setStrokeColor(COLORS.accent + (active ? '' : '60'));
    arc(radius, -IN_TUNE_CENTS, IN_TUNE_CENTS);
    // Ticks every 10 cents, longer at 0 and the ends.
    canvas.setStrokeWidth(2);
    for (let cents = -TUNER_RANGE; cents <= TUNER_RANGE; cents += 10) {
        const major = cents === 0 || Math.abs(cents) === TUNER_RANGE;
        const [x1, y1] = point(cents, radius - 9);
        const [x2, y2] = point(cents, radius - (major ? 22 : 15));
        canvas.setStrokeColor(major ? COLORS.text : COLORS.dim);
        canvas.drawLine(x1, y1, x2, y2);
    }
    canvas.setFillColor(COLORS.dim);
    for (const cents of [-TUNER_RANGE, TUNER_RANGE]) {
        const label = (cents > 0 ? '+' : '') + cents;
        const [lx, ly] = point(cents, radius + 14);
        canvas.drawText(label, lx - canvas.measureText(label, 11) / 2, ly + 4, 11);
    }

    // The needle: teal in tune, amber otherwise.
    const inTune = active && Math.abs(needleCents) <= IN_TUNE_CENTS;
    const color = !active ? COLORS.dim : inTune ? COLORS.accent : COLORS.monitor;
    const [nx, ny] = point(Math.max(-TUNER_RANGE, Math.min(TUNER_RANGE, needleCents)), radius - 4);
    canvas.setStrokeColor(color);
    canvas.setStrokeWidth(3);
    canvas.drawLine(cx, cy, nx, ny);
    canvas.setStrokeCap('butt');
    canvas.setFillColor(color);
    canvas.drawCircle(cx, cy, 6);
}

const AXIS_WIDTH = 40;

// Frequency labels, and a mark at the pitch; the spectrogram
// itself is drawn with GL over the canvas (drawSpectrogram).
function drawSpectrogramAxis(x, y, w, h, now) {
    const canvas = sys.canvas;
    canvas.setFillColor(COLORS.panel);
    canvas.drawRoundRect(x, y, w, h, 8, 8);
    const rowY = (frequency) => y + h - analyzer.rowOf(frequency) / ROWS * h;
    canvas.setFillColor(COLORS.dim);
    for (const [frequency, label] of [[100, '100'], [200, '200'], [500, '500'], [1000, '1k'], [2000, '2k'], [5000, '5k'], [10000, '10k']]) {
        if (frequency > analyzer.maxFrequency) continue;
        canvas.drawText(label, x + 6, rowY(frequency) + 4, 11);
    }
    if (now - pitchTime < 1) {
        canvas.setFillColor(COLORS.accent);
        const my = rowY(pitch);
        canvas.drawRect(x + AXIS_WIDTH - 8, my - 1.5, 8, 3);
    }
}

function drawSpectrogramNotes(x, y, w, h) {
    const canvas = sys.canvas;
    const step = w / visibleColumns;
    const first = Math.max(0, columnCount - visibleColumns);
    const size = 12;
    canvas.save();
    canvas.clipRect(x, y, w, h);
    // Leave only the labels here; the shader composites them over the FFT.
    canvas.clear('#00000000');
    for (let column = first; column < columnCount;) {
        const frequency = pitchHistory[column % COLUMNS];
        if (frequency <= 0) {
            column++;
            continue;
        }
        const note = noteOf(frequency);
        const start = column++;
        while (column < columnCount) {
            const next = pitchHistory[column % COLUMNS];
            if (next <= 0 || noteOf(next).name !== note.name) break;
            column++;
        }
        const label = note.name.replace(/-?\d+$/, '');
        const labelW = canvas.measureText(label, size) + 8;
        const labelH = size + 6;
        const cx = x + ((start + column) / 2 - (columnCount - visibleColumns)) * step;
        const cy = y + h - analyzer.rowOf(frequency) / ROWS * h;
        const left = Math.max(x, Math.min(x + w - labelW, cx - labelW / 2));
        const top = Math.max(y, Math.min(y + h - labelH, cy - labelH / 2));
        canvas.setFillColor(COLORS.background + 'dd');
        canvas.drawRoundRect(left, top, labelW, labelH, 4, 4);
        canvas.setFillColor(COLORS.accent);
        canvas.drawText(label, left + 4, top + size + 1, size);
    }
    canvas.restore();
}

function drawSpectrogram(x, y, w, h) {
    if (spectrumTexture < 0) {
        spectrumTexture = sys.gl.createTexture2D(COLUMNS, ROWS, 'r8', spectrogram);
        spectrumShader = sys.gl.createProgram('spectrogram.vert', 'spectrogram.frag');
        if (spectrumTexture < 0 || spectrumShader < 0) sys.log('Spectrogram unavailable: ' + sys.gl.getLastError());
    }
    if (!analyzer || spectrumTexture < 0 || spectrumShader <= 0) return;
    // Upload the columns computed since the last frame.
    const pending = columnCount - spectrumUploaded;
    if (pending >= COLUMNS) {
        sys.gl.updateTexture2D(spectrumTexture, 0, 0, COLUMNS, ROWS, transposed());
    } else {
        for (let n = spectrumUploaded; n < columnCount; n++) {
            const column = n % COLUMNS;
            sys.gl.updateTexture2D(spectrumTexture, column, 0, 1, ROWS, spectrogram.subarray(column * ROWS, (column + 1) * ROWS));
        }
    }
    spectrumUploaded = columnCount;
    sys.gl.bindScreen();
    sys.gl.useProgram(spectrumShader);
    sys.gl.texture(spectrumShader, 'u_spectrum', spectrumTexture, 1);
    sys.gl.setUniform1f(spectrumShader, 'u_newest', (columnCount + COLUMNS - 1) % COLUMNS);
    sys.gl.setUniform1f(spectrumShader, 'u_columns', COLUMNS);
    sys.gl.setUniform1f(spectrumShader, 'u_visibleColumns', visibleColumns);
    sys.gl.setUniform1f(spectrumShader, 'u_rows', ROWS);
    sys.gl.drawRegionImmediate(spectrumShader, x * UI_SCALE, y * UI_SCALE, w * UI_SCALE, h * UI_SCALE, -1);
}

// The ring is stored column by column; a texture is row by row.
let transposedPixels = null;
function transposed() {
    transposedPixels ??= new Uint8Array(COLUMNS * ROWS);
    for (let c = 0; c < COLUMNS; c++)
        for (let r = 0; r < ROWS; r++) transposedPixels[r * COLUMNS + c] = spectrogram[c * ROWS + r];
    return transposedPixels;
}

// Bottom toolbar: time segments, FFT dropdown, and Monitor.
let options = [];
let timeOptions = [];

function fftLabel(choice) {
    return choice === 'auto' ? 'Auto' : choice === 'off' ? 'Off' : choice >= 1024 ? choice / 1024 + 'k' : String(choice);
}

function drawOptions(items, choice, autoChoice = null) {
    const canvas = sys.canvas;
    const first = items[0], last = items[items.length - 1];
    canvas.setFillColor(COLORS.panel);
    canvas.drawRoundRect(first.x, first.y, last.x + last.w - first.x, last.y + last.h - first.y, 10, 10);
    for (const o of items) {
        const selected = o.choice === choice;
        // Under Auto, its current size is outlined.
        const automatic = o.choice === autoChoice;
        if (selected) {
            canvas.setFillColor(COLORS.accent);
            canvas.drawRoundRect(o.x + 2, o.y + 2, o.w - 4, o.h - 4, 8, 8);
        } else if (automatic) {
            canvas.setStrokeColor(COLORS.accent + '90');
            canvas.setStrokeWidth(1.5);
            canvas.drawRoundRect(o.x + 3, o.y + 3, o.w - 6, o.h - 6, 7, 7);
        }
        canvas.setFillColor(selected ? COLORS.background : automatic ? COLORS.accent : COLORS.text);
        const size = 14 * Math.min(1, (o.w - 6) / canvas.measureText(o.label, 14));
        canvas.drawText(o.label, o.x + (o.w - canvas.measureText(o.label, size)) / 2, o.y + o.h / 2 + size * 0.35, size);
    }
}

function drawFftSelect() {
    const canvas = sys.canvas;
    const b = fftButton;
    canvas.setFillColor(COLORS.button);
    canvas.drawRoundRect(b.x, b.y, b.w, b.h, 10, 10);
    canvas.setFillColor(COLORS.dim);
    canvas.drawText('FFT', b.x + 8, b.y + 15, 11);
    canvas.setFillColor(COLORS.text);
    canvas.drawText(fftLabel(fftChoice), b.x + 8, b.y + 36, 14);
    canvas.setStrokeColor(fftMenuOpen ? COLORS.accent : COLORS.dim);
    canvas.setStrokeWidth(1.5);
    const cx = b.x + b.w - 12, cy = b.y + b.h / 2;
    const direction = fftMenuOpen ? -1 : 1;
    canvas.drawLine(cx - 4, cy - direction * 2, cx, cy + direction * 2);
    canvas.drawLine(cx, cy + direction * 2, cx + 4, cy - direction * 2);
    if (fftMenuOpen) drawOptions(options, fftChoice, fftChoice === 'auto' ? autoSize : null);
}

function statusLine() {
    if (mic < 0) return micError || 'Microphone closed';
    if (!micInfo) return 'Waiting for the microphone...';
    let line = `${micInfo.sampleRate} Hz · input queue ${(micInfo.latency * 1000).toFixed(0)} ms`;
    if (micInfo.overruns) line += ` · ${micInfo.overruns} dropped`;
    if (analyzer) line += ` · FFT ${analyzer.size}${fftChoice === 'auto' ? ' (auto)' : ''} ${analysisMs.toFixed(1)} ms`;
    if (monitorStream >= 0) line += ` · monitor +${(monitorLatency * 1000).toFixed(0)} ms`;
    return line;
}

// ============ Frame loop ============

let wasFocused = true;

function frame() {
    const input = sys.input.get();
    const width = sys.window.getWidth() / UI_SCALE;
    const height = sys.window.getHeight() / UI_SCALE;

    // Back from the permission dialog (or any other window): try again.
    if (input.focused && !wasFocused && mic < 0) openMic();
    wasFocused = input.focused;

    const now = input.totalTime;
    if (level >= peakHold || now - peakHoldTime > 1.5) {
        peakHold = level;
        peakHoldTime = now;
    }
    // The meter falls back to zero when nothing arrives.
    if (mic < 0) level *= 0.9;

    adjustAuto(now);
    const toolbarTop = layoutToolbar(width, height);
    const pad = 16;
    const contentW = width - pad * 2;
    if (!input.focused || sys.input.isKeyPressed(41)) fftMenuOpen = false;

    if (input.pointer.pressed) {
        const px = input.pointer.x / UI_SCALE;
        const py = input.pointer.y / UI_SCALE;
        let hit = false;
        if (fftMenuOpen) {
            const option = options.find(o => inside(o, px, py));
            if (option && option.choice !== fftChoice) chooseFft(option.choice);
            fftMenuOpen = false;
            hit = true;
        } else if (inside(fftButton, px, py)) {
            fftMenuOpen = true;
            hit = true;
        }
        if (!hit) {
            for (const b of buttons) {
                if (b.enabled && inside(b, px, py)) {
                    b.action();
                    hit = true;
                }
            }
            for (const o of timeOptions) {
                if (inside(o, px, py)) {
                    timeSpan = o.choice;
                    visibleColumns = Math.round(timeSpan * SAMPLE_RATE / COLUMN_SAMPLES);
                    hit = true;
                }
            }
        }
        if (!hit && mic < 0) openMic();
    }

    const canvas = sys.canvas;
    canvas.clear(COLORS.background);
    canvas.save();
    canvas.scale(UI_SCALE, UI_SCALE);

    canvas.setFillColor(COLORS.text);
    canvas.drawText('Microphone', pad, 44, 28);
    canvas.setFillColor(mic < 0 ? COLORS.danger : COLORS.dim);
    canvas.drawText(statusLine(), pad, 70, 14);

    if (mic < 0) {
        canvas.setFillColor(COLORS.text);
        canvas.drawText('Tap to open the microphone', pad, 110, 18);
    }

    drawMeter(pad, 120, contentW, 22);
    drawPitch(pad, 194, contentW, now);
    const tunerH = 96;
    drawTuner(pad, 206, contentW, tunerH, now, input.deltaTime);
    // The waveform takes a third of the space, the spectrogram the rest;
    // both start after the frequency axis, so their columns line up. With
    // the FFT off, the waveform takes it all.
    const graphsTop = 206 + tunerH + 12;
    const graphsH = Math.max(160, toolbarTop - 12 - graphsTop);
    const spectrumOn = analyzer !== null;
    const waveH = spectrumOn ? Math.round(graphsH * 0.32) : graphsH;
    const spectrumTop = graphsTop + waveH + 12;
    const spectrumH = graphsH - waveH - 12;
    const graphX = spectrumOn ? pad + AXIS_WIDTH : pad;
    const graphW = spectrumOn ? contentW - AXIS_WIDTH : contentW;
    drawWaveform(graphX, graphsTop, graphW, waveH);
    if (spectrumOn) {
        drawSpectrogramAxis(pad, spectrumTop, contentW, spectrumH, now);
        drawSpectrogramNotes(graphX, spectrumTop, graphW, spectrumH);
    }
    if (width >= 380) {
        canvas.setFillColor(COLORS.dim);
        canvas.drawText('Time', pad, toolbarTop + 28, 12);
    }
    drawOptions(timeOptions, timeSpan);
    for (const b of buttons) drawButton(b);
    drawFftSelect();

    canvas.restore();
    if (spectrumOn) drawSpectrogram(graphX, spectrumTop, graphW, spectrumH);
    sys.animation.requestFrame(frame);
}

openMic();
sys.animation.requestFrame(frame);
