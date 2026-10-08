/**
 * MIDI Tempo
 *
 * Listens to every MIDI input and estimates the tempo you play, between 50
 * and 150 BPM (see tempo.js), so you can check you hold the tempo you want:
 * - the estimated BPM, how sure it is, and the other readings that fit;
 * - a target tempo, and how far ahead or behind you are;
 * - the last seconds of notes on the estimated beat grid, and a beat light;
 * - the last minute of estimates against the target;
 * - the tempo of the MIDI clock, when a device sends one.
 *
 * No MIDI device? Tap the screen or press Space to play the beat.
 */

import { TempoEstimator, MIN_BPM, MAX_BPM } from './tempo.js';

const UI_SCALE = Math.max(1, sys.window.getDisplayDensity());
const SPACE = 44;
const TIMELINE_SECONDS = 6;
const HISTORY_SECONDS = 60;
// Re-estimate at most this often (the estimate costs a few milliseconds).
const ESTIMATE_EVERY_MS = 120;

const COLORS = {
    background: '#10131c',
    panel: '#1a1f2c',
    text: '#e8ecf4',
    dim: '#8892a6',
    accent: '#7dd3fc',
    good: '#4ade80',
    warn: '#fbbf24',
    bad: '#f87171',
    button: '#262d3d',
};

const tempo = new TempoEstimator();
let target = 100;
let estimateDue = false;
let lastEstimateAt = -1e9;
let shownBpm = 0; // the displayed BPM, eased toward the estimate

/** @type {{time: number, velocity: number}[]} recent notes, in ms (performance.now) */
const notes = [];
/** @type {{time: number, bpm: number}[]} */
const history = [];

// ============ MIDI ============

let inputs = []; // { handle, name }
let midiNames = [];
// MIDI timestamps are microseconds on their own clock: offset maps them to
// performance.now(). The smallest offset seen is the one with the least delay.
let clockOffset = null;
// MIDI clock: 24 ticks per beat.
const clockTicks = [];
let clockBpm = 0;
let clockSeenAt = -1e9;

function toNow(msg) {
    const now = performance.now();
    if (!msg.timestamp) return now;
    const at = msg.timestamp / 1000;
    const offset = now - at;
    if (clockOffset === null || offset < clockOffset) clockOffset = offset;
    // Drift between the clocks: let the offset rise slowly too.
    clockOffset += (offset - clockOffset) * 0.001;
    return at + clockOffset;
}

function onMidi(msg) {
    const status = msg.status;
    if (status === 0xf8) {
        const t = toNow(msg);
        clockTicks.push(t);
        while (clockTicks.length > 49) clockTicks.shift();
        if (clockTicks.length >= 25) {
            const span = clockTicks[clockTicks.length - 1] - clockTicks[0];
            clockBpm = 60000 / (span / (clockTicks.length - 1) * 24);
        }
        clockSeenAt = performance.now();
        return;
    }
    if ((status & 0xf0) === 0x90 && msg.data2 > 0) onset(toNow(msg), msg.data2 / 127);
}

function openInputs() {
    for (const input of inputs) sys.midi.closeInput(input.handle);
    inputs = [];
    sys.midi.refreshDevices();
    const devices = sys.midi.getInputDevices();
    devices.forEach((device, index) => {
        const handle = sys.midi.openInput(index, onMidi);
        if (handle >= 0) inputs.push({ handle, name: device.name });
    });
    midiNames = inputs.map((input) => input.name);
}

if (sys.midi) {
    openInputs();
    sys.midi.onDevicesChanged(() => openInputs());
}

// ============ Onsets ============

function onset(timeMs, velocity) {
    notes.push({ time: timeMs, velocity });
    while (notes.length && timeMs - notes[0].time > TIMELINE_SECONDS * 1000 + 1000) notes.shift();
    tempo.addOnset(timeMs / 1000, velocity);
    estimateDue = true;
}

function estimate(now) {
    if (!estimateDue || now - lastEstimateAt < ESTIMATE_EVERY_MS) return;
    estimateDue = false;
    lastEstimateAt = now;
    tempo.estimate(now / 1000);
    if (tempo.bpm > 0) {
        history.push({ time: now, bpm: tempo.bpm });
        while (history.length && now - history[0].time > HISTORY_SECONDS * 1000) history.shift();
    }
}

function reset() {
    tempo.clear();
    notes.length = 0;
    history.length = 0;
    shownBpm = 0;
}

// ============ Interface ============

let buttons = [];

function button(id, label, x, y, w, h, action, selected = false) {
    buttons.push({ id, label, x, y, w, h, action, selected });
}

function drawButtons() {
    const canvas = sys.canvas;
    for (const b of buttons) {
        canvas.setFillColor(b.selected ? COLORS.accent : COLORS.button);
        canvas.drawRoundRect(b.x, b.y, b.w, b.h, 8, 8);
        canvas.setFillColor(b.selected ? COLORS.background : COLORS.text);
        const size = 15;
        canvas.drawText(b.label, b.x + (b.w - canvas.measureText(b.label, size)) / 2, b.y + b.h / 2 + 5, size);
    }
}

const setTarget = (value) => { target = Math.max(MIN_BPM, Math.min(MAX_BPM, Math.round(value))); };

function deviationText(bpm) {
    const off = bpm - target;
    if (Math.abs(off) < 1) return { text: 'on tempo', color: COLORS.good };
    const text = `${off > 0 ? '+' : ''}${off.toFixed(1)} BPM ${off > 0 ? 'ahead' : 'behind'}`;
    return { text, color: Math.abs(off) < 3 ? COLORS.warn : COLORS.bad };
}

function drawTempo(x, y, w, now) {
    const canvas = sys.canvas;
    const has = tempo.bpm > 0;
    if (has) {
        // Ease small changes; jump when the reading changes a lot.
        shownBpm = shownBpm && Math.abs(tempo.bpm - shownBpm) < 4 ? shownBpm + (tempo.bpm - shownBpm) * 0.15 : tempo.bpm;
    }
    canvas.setFillColor(has ? COLORS.text : COLORS.dim);
    const big = has ? shownBpm.toFixed(1) : '--';
    canvas.drawText(big, x, y + 64, 72);
    const bigW = canvas.measureText(big, 72);
    canvas.setFillColor(COLORS.dim);
    canvas.drawText('BPM', x + bigW + 10, y + 64, 22);

    // The beat light: flashes on the estimated beats.
    if (has) {
        const period = 60000 / tempo.bpm;
        let phase = ((now - tempo.phase * 1000) / period) % 1;
        if (phase < 0) phase += 1;
        const flash = Math.max(0, 1 - phase * 4);
        canvas.setFillColor(COLORS.panel);
        canvas.drawCircle(x + w - 34, y + 40, 26);
        canvas.setAlpha(Math.round(60 + 195 * flash));
        canvas.setFillColor(COLORS.accent);
        canvas.drawCircle(x + w - 34, y + 40, 12 + 12 * flash);
        canvas.setAlpha(255);
    }

    // How sure, and what else fits.
    const barY = y + 82;
    canvas.setFillColor(COLORS.panel);
    canvas.drawRoundRect(x, barY, w, 8, 4, 4);
    const confidence = has ? tempo.confidence : 0;
    canvas.setFillColor(confidence > 0.6 ? COLORS.good : confidence > 0.3 ? COLORS.warn : COLORS.bad);
    canvas.drawRoundRect(x, barY, Math.max(0.1, w * confidence), 8, 4, 4);
    canvas.setFillColor(COLORS.dim);
    let line = has ? `confidence ${Math.round(confidence * 100)}%` : 'Play a few notes on the beat (or tap)';
    if (has && tempo.alternatives.length)
        line += ` · could also be ${tempo.alternatives.map((b) => b.toFixed(0)).join(' or ')}`;
    canvas.drawText(line, x, barY + 26, 14);
    if (now - clockSeenAt < 2000 && clockBpm > 0) {
        const clock = `MIDI clock ${clockBpm.toFixed(1)}`;
        canvas.setFillColor(COLORS.accent);
        canvas.drawText(clock, x + w - canvas.measureText(clock, 14), barY + 26, 14);
    }
}

function drawTarget(x, y, w) {
    const canvas = sys.canvas;
    canvas.setFillColor(COLORS.dim);
    canvas.drawText('Target', x, y + 24, 15);
    const bw = 44, gap = 6, h = 36;
    let bx = x + 60;
    button('t-5', '−5', bx, y, bw, h, () => setTarget(target - 5)); bx += bw + gap;
    button('t-1', '−1', bx, y, bw, h, () => setTarget(target - 1)); bx += bw + gap;
    canvas.setFillColor(COLORS.text);
    const value = String(target);
    canvas.drawText(value, bx + (56 - canvas.measureText(value, 24)) / 2, y + 27, 24);
    bx += 56 + gap;
    button('t+1', '+1', bx, y, bw, h, () => setTarget(target + 1)); bx += bw + gap;
    button('t+5', '+5', bx, y, bw, h, () => setTarget(target + 5)); bx += bw + gap;
    if (bx + 64 <= x + w) button('use', 'Use', bx, y, 64, h, () => tempo.bpm > 0 && setTarget(tempo.bpm));
    if (tempo.bpm > 0) {
        const d = deviationText(tempo.bpm);
        canvas.setFillColor(d.color);
        canvas.drawText(d.text, x, y + h + 30, 22);
    }
}

// The last seconds: each note a tick (taller when louder), the estimated
// beats as lines, and the target's beats, aligned on the same first beat, as
// dots: when they drift apart, you are off the target.
function drawTimeline(x, y, w, h, now) {
    const canvas = sys.canvas;
    canvas.setFillColor(COLORS.panel);
    canvas.drawRoundRect(x, y, w, h, 8, 8);
    const start = now - TIMELINE_SECONDS * 1000;
    const toX = (t) => x + (t - start) / (TIMELINE_SECONDS * 1000) * w;
    if (tempo.bpm > 0) {
        const period = 60000 / tempo.bpm;
        const phaseMs = tempo.phase * 1000;
        canvas.setStrokeColor('#3a4560');
        canvas.setStrokeWidth(1.5);
        let k = Math.ceil((start - phaseMs) / period);
        for (let t = phaseMs + k * period; t < now; t += period) canvas.drawLine(toX(t), y + 6, toX(t), y + h - 6);
        // The target grid, from the estimated beat nearest the start of the view.
        const anchor = phaseMs + k * period;
        const targetPeriod = 60000 / target;
        canvas.setFillColor(COLORS.warn);
        for (let t = anchor; t < now; t += targetPeriod) canvas.drawCircle(toX(t), y + h - 8, 3);
    }
    canvas.setStrokeColor(COLORS.accent);
    canvas.setStrokeWidth(3);
    canvas.setStrokeCap('round');
    for (const n of notes) {
        if (n.time < start) continue;
        const nx = toX(n.time);
        canvas.drawLine(nx, y + h / 2 - n.velocity * (h / 2 - 12), nx, y + h / 2 + n.velocity * (h / 2 - 12));
    }
    canvas.setStrokeCap('butt');
}

// The last minute of estimates, against the target.
function drawHistory(x, y, w, h, now) {
    const canvas = sys.canvas;
    canvas.setFillColor(COLORS.panel);
    canvas.drawRoundRect(x, y, w, h, 8, 8);
    let low = target - 10, high = target + 10;
    for (const p of history) {
        low = Math.min(low, p.bpm - 2);
        high = Math.max(high, p.bpm + 2);
    }
    const toY = (bpm) => y + h - 8 - (bpm - low) / (high - low) * (h - 16);
    const toX = (t) => x + w - (now - t) / (HISTORY_SECONDS * 1000) * w;
    canvas.setStrokeColor(COLORS.warn + '90');
    canvas.setStrokeWidth(1.5);
    canvas.drawLine(x + 4, toY(target), x + w - 4, toY(target));
    canvas.setFillColor(COLORS.dim);
    canvas.drawText(String(target), x + 6, toY(target) - 4, 11);
    canvas.setStrokeColor(COLORS.accent);
    canvas.setStrokeWidth(2);
    for (let i = 1; i < history.length; i++) {
        const a = history[i - 1], b = history[i];
        if (b.time - a.time > 3000) continue; // a pause: no line across it
        canvas.drawLine(toX(a.time), toY(a.bpm), toX(b.time), toY(b.bpm));
    }
    canvas.setFillColor(COLORS.dim);
    canvas.drawText('last minute', x + w - 74, y + 16, 11);
}

// ============ Frame ============

function frame() {
    const input = sys.input.get();
    const now = performance.now();
    const width = sys.window.getWidth() / UI_SCALE;
    const height = sys.window.getHeight() / UI_SCALE;
    const pad = 16, contentW = width - pad * 2;

    if (sys.input.isKeyPressed(SPACE)) onset(now, 0.8);
    estimate(now);

    const canvas = sys.canvas;
    canvas.clear(COLORS.background);
    canvas.save();
    canvas.scale(UI_SCALE, UI_SCALE);
    buttons = [];

    canvas.setFillColor(COLORS.text);
    canvas.drawText('MIDI Tempo', pad, 40, 26);
    canvas.setFillColor(COLORS.dim);
    const source = midiNames.length ? 'Listening to ' + midiNames.join(', ') : 'No MIDI input: tap or press Space on the beat';
    canvas.drawText(source, pad, 64, 13);
    button('reset', 'Reset', width - pad - 70, 18, 70, 32, reset);

    drawTempo(pad, 76, contentW, now);
    drawTarget(pad, 214, contentW);
    const timelineTop = 300, bottom = height - pad;
    const space = Math.max(160, bottom - timelineTop);
    const timelineH = Math.round(space * 0.45);
    drawTimeline(pad, timelineTop, contentW, timelineH, now);
    drawHistory(pad, timelineTop + timelineH + 12, contentW, space - timelineH - 12, now);
    drawButtons();
    canvas.restore();

    // Taps: buttons, or a beat anywhere else.
    if (input.pointer.pressed) {
        const px = input.pointer.x / UI_SCALE, py = input.pointer.y / UI_SCALE;
        const hit = buttons.find((b) => px >= b.x && px <= b.x + b.w && py >= b.y && py <= b.y + b.h);
        if (hit) hit.action();
        else onset(now, 0.8);
    }
    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
