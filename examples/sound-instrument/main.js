/// <reference path="./budo.d.ts" />

// A touch instrument: every finger (or the mouse) plays its own oscillator.
// Left to right is the pitch, bottom to top the volume. Each voice glides
// toward its finger at the chosen speed, and fades out when the finger lifts.
// The panel at the top sets the oscillator type and the glide speed, and,
// where the device has an accelerometer, a pitch bend by tilting it sideways:
// right edge down bends up, left edge down bends down, progressively.

sys.audio.setMasterGain(1);

const MAX_FREQUENCY = 880;
const MAX_VOICES = 10;
// Several voices add up: each one is quieter so all of them stay under 1.
const VOICE_GAIN = 0.35;

// Laid out in density-independent pixels, like budo-ui.
const UI_SCALE = Math.max(1, sys.window.getDisplayDensity());
const PANEL_HEIGHT = 168;

const TYPES = ['sine', 'triangle', 'square', 'sawtooth'];
const VOICE_COLORS = ['#bc11e7', '#00adb5', '#ff9f1c', '#e71d36', '#2ec4b6', '#f4d35e', '#9b5de5', '#00bbf9', '#f15bb5', '#83c5be'];

let oscillatorType = 'sawtooth';
// Speed, 0 (slow glide) to 1 (instant): the time constant of the glide.
let speed = 0.6;
const glideSeconds = () => 1.5 * (1 - speed) * (1 - speed) + 0.005;

// ============ Tilt bend ============

// The bend range in semitones: 0 is off.
const BEND_RANGES = [0, 2, 12];
const BEND_FULL_DEGREES = 30; // this much tilt bends the whole range
const BEND_DEAD_DEGREES = 3;  // a steady hand stays in tune
const hasTilt = sys.sensors.hasAccelerometer();
let bendRange = 0;
let bendCenter = null; // the tilt angle at the start: no bend
let tiltAngle = 0;     // degrees, smoothed
let bend = 0;          // semitones, applied to every voice
let tiltSize = '';     // the window size, to recenter when the screen rotates

function setBendRange(range) {
    bendRange = range;
    bendCenter = null; // recenter on the next sample
    bend = 0;
    if (range > 0) sys.sensors.start();
    else sys.sensors.stop();
}

// The sideways tilt: the angle of the screen's left-right axis against the
// horizontal, so a tilt bends progressively however the device is held (flat,
// tilted back, or upright). Which way is left-right on the screen is taken
// from gravity when the bend starts: "down" on the screen is where gravity
// pulls in the screen's plane (or, held flat, the bottom edge).
let screenRight = null; // unit vector in the device's x, y plane

function updateBend(dt, width, height) {
    if (bendRange === 0) return;
    const size = width > height ? 'landscape' : 'portrait';
    if (size !== tiltSize) {
        tiltSize = size;
        bendCenter = null;
    }
    const accel = sys.sensors.getAccel();
    if (!accel) return;
    const g = Math.hypot(accel.x, accel.y, accel.z);
    if (g < 4) return; // in free fall, or no reading yet
    if (bendCenter === null) {
        // The reading points up (it is the reaction to gravity): the
        // screen's down is opposite its part in the screen's plane.
        const planar = Math.hypot(accel.x, accel.y);
        const down = planar > 3 ? [-accel.x / planar, -accel.y / planar]
            : size === 'portrait' ? [0, -1] : [-1, 0];
        screenRight = [-down[1], down[0]];
    }
    // The up reading's part along the screen's right: positive when the
    // right edge is up.
    const along = (accel.x * screenRight[0] + accel.y * screenRight[1]) / g;
    const angle = Math.asin(Math.max(-1, Math.min(1, along))) * 180 / Math.PI;
    if (bendCenter === null) {
        bendCenter = angle;
        tiltAngle = angle;
    }
    // Smooth the sensor's jitter (about 60 ms).
    tiltAngle += (angle - tiltAngle) * (1 - Math.exp(-dt / 0.06));
    // The right edge going down (a clockwise turn) bends up.
    const turn = bendCenter - tiltAngle;
    const active = Math.sign(turn) * Math.max(0, Math.abs(turn) - BEND_DEAD_DEGREES);
    bend = bendRange * Math.max(-1, Math.min(1, active / (BEND_FULL_DEGREES - BEND_DEAD_DEGREES)));
}

const bendFactor = () => Math.pow(2, bend / 12);

// ============ Voices ============

/** @type {Map<number, {osc: number, freq: number, gain: number, targetFreq: number, targetGain: number, held: boolean, color: string}>} */
const voices = new Map();
let nextColor = 0;

function startVoice(id, freq, gain) {
    if (voices.size >= MAX_VOICES) return null;
    const osc = sys.audio.createOscillator();
    if (osc < 0) return null;
    sys.audio.setOscillatorType(osc, oscillatorType);
    // Starts at its finger's pitch, from silence.
    sys.audio.setOscillatorFrequency(osc, freq * bendFactor());
    sys.audio.setOscillatorGain(osc, 0);
    sys.audio.startOscillator(osc);
    const voice = { osc, freq, gain: 0, targetFreq: freq, targetGain: gain, held: true,
        color: VOICE_COLORS[nextColor++ % VOICE_COLORS.length] };
    voices.set(id, voice);
    return voice;
}

function updateVoices(dt) {
    // The same glide whatever the frame rate.
    const t = 1 - Math.exp(-dt / glideSeconds());
    for (const [id, voice] of voices) {
        voice.gain += (voice.targetGain - voice.gain) * t;
        voice.freq += (voice.targetFreq - voice.freq) * t;
        // Released and faded out: free the oscillator.
        if (!voice.held && voice.gain < 0.001) {
            sys.audio.stopOscillator(voice.osc);
            sys.audio.destroyOscillator(voice.osc);
            voices.delete(id);
            continue;
        }
        sys.audio.setOscillatorGain(voice.osc, voice.gain * VOICE_GAIN);
        sys.audio.setOscillatorFrequency(voice.osc, voice.freq * bendFactor());
    }
}

function setType(type) {
    oscillatorType = type;
    for (const voice of voices.values()) sys.audio.setOscillatorType(voice.osc, type);
}

// ============ Panel ============

const pad = 16;
let typeButtons = [];
let slider = { x: 0, y: 0, w: 0, h: 0 };
let bendButtons = [];

function layoutPanel(width) {
    const gap = 8;
    const w = (width - pad * 2 - gap * (TYPES.length - 1)) / TYPES.length;
    typeButtons = TYPES.map((type, i) => ({ type, x: pad + i * (w + gap), y: 40, w, h: 36 }));
    slider = { x: pad + 64, y: 96, w: width - pad * 2 - 64 - 70, h: 24 };
    const bw = 64;
    bendButtons = BEND_RANGES.map((range, i) => ({ range, x: pad + 64 + i * (bw + gap), y: 128, w: bw, h: 30 }));
}

const inside = (r, x, y) => x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;

function drawPanel(width) {
    const canvas = sys.canvas;
    canvas.setFillColor('#1b2029');
    canvas.drawRect(0, 0, width, PANEL_HEIGHT);
    canvas.setFillColor('#eeeeee');
    canvas.drawText('Touch to play: one voice per finger', pad, 26, 16);

    for (const b of typeButtons) {
        const selected = b.type === oscillatorType;
        canvas.setFillColor(selected ? '#00adb5' : '#393e46');
        canvas.drawRoundRect(b.x, b.y, b.w, b.h, 8, 8);
        canvas.setFillColor(selected ? '#222831' : '#eeeeee');
        const size = b.w < 70 ? 13 : 15;
        canvas.drawText(b.type, b.x + (b.w - canvas.measureText(b.type, size)) / 2, b.y + 23, size);
    }

    canvas.setFillColor('#aaaaaa');
    canvas.drawText('Speed', pad, slider.y + 17, 15);
    const mid = slider.y + slider.h / 2;
    canvas.setFillColor('#393e46');
    canvas.drawRoundRect(slider.x, mid - 3, slider.w, 6, 3, 3);
    canvas.setFillColor('#00adb5');
    canvas.drawRoundRect(slider.x, mid - 3, slider.w * speed, 6, 3, 3);
    canvas.drawCircle(slider.x + slider.w * speed, mid, 10);
    canvas.setFillColor('#aaaaaa');
    const glide = glideSeconds();
    canvas.drawText(glide < 0.01 ? 'instant' : glide.toFixed(2) + ' s', slider.x + slider.w + 16, slider.y + 17, 14);

    canvas.setFillColor('#aaaaaa');
    canvas.drawText('Tilt', pad, 148, 15);
    if (!hasTilt) {
        canvas.drawText('no accelerometer on this device', pad + 64, 148, 14);
        return;
    }
    for (const b of bendButtons) {
        const selected = b.range === bendRange;
        canvas.setFillColor(selected ? '#00adb5' : '#393e46');
        canvas.drawRoundRect(b.x, b.y, b.w, b.h, 8, 8);
        canvas.setFillColor(selected ? '#222831' : '#eeeeee');
        const label = b.range === 0 ? 'off' : '±' + b.range;
        canvas.drawText(label, b.x + (b.w - canvas.measureText(label, 14)) / 2, b.y + 20, 14);
    }
    if (bendRange > 0) {
        // The bend: a needle on a short scale, and its value.
        const last = bendButtons[bendButtons.length - 1];
        const x0 = last.x + last.w + 20, x1 = width - pad - 48, mid = 143;
        if (x1 - x0 > 30) {
            canvas.setFillColor('#393e46');
            canvas.drawRoundRect(x0, mid - 2, x1 - x0, 4, 2, 2);
            canvas.setFillColor('#eeeeee');
            canvas.drawRect((x0 + x1) / 2 - 1, mid - 7, 2, 14);
            canvas.setFillColor('#00adb5');
            canvas.drawCircle(x0 + (x1 - x0) * (0.5 + bend / bendRange / 2), mid, 7);
        }
        canvas.setFillColor('#aaaaaa');
        const value = (bend >= 0 ? '+' : '') + bend.toFixed(1);
        canvas.drawText(value, width - pad - canvas.measureText(value, 14), 148, 14);
    }
}

// ============ Frame ============

// Pointers that went down on the panel drive it, not a voice.
const panelPointers = new Set();
let sliderPointer = null;

function frame() {
    const input = sys.input.get();
    const width = sys.window.getWidth() / UI_SCALE;
    const height = sys.window.getHeight() / UI_SCALE;
    layoutPanel(width);

    const playTop = PANEL_HEIGHT;
    const playHeight = Math.max(1, height - playTop);
    const seen = new Set();
    for (const p of input.pointers) {
        const x = p.x / UI_SCALE, y = p.y / UI_SCALE;
        seen.add(p.id);
        if (p.pressed && y < PANEL_HEIGHT) {
            panelPointers.add(p.id);
            const button = typeButtons.find((b) => inside(b, x, y));
            if (button) setType(button.type);
            const bendButton = hasTilt && bendButtons.find((b) => inside(b, x, y));
            if (bendButton) setBendRange(bendButton.range);
            if (inside({ ...slider, x: slider.x - 12, w: slider.w + 24 }, x, y)) sliderPointer = p.id;
        }
        if (panelPointers.has(p.id)) {
            if (sliderPointer === p.id) speed = Math.max(0, Math.min(1, (x - slider.x) / slider.w));
            continue;
        }
        const targetFreq = MAX_FREQUENCY * Math.max(0, Math.min(1, x / width));
        const targetGain = Math.max(0, Math.min(1, 1 - (y - playTop) / playHeight));
        let voice = voices.get(p.id);
        if (voice && !voice.held) {
            // A new touch reusing the id of a fading voice: replace it.
            sys.audio.stopOscillator(voice.osc);
            sys.audio.destroyOscillator(voice.osc);
            voices.delete(p.id);
            voice = undefined;
        }
        if (!voice) voice = startVoice(p.id, targetFreq, targetGain);
        if (voice) {
            voice.targetFreq = targetFreq;
            voice.targetGain = targetGain;
        }
    }
    // Lifted fingers: their voices fade out.
    for (const [id, voice] of voices) {
        if (!seen.has(id)) {
            voice.held = false;
            voice.targetGain = 0;
        }
    }
    for (const id of panelPointers) if (!seen.has(id)) panelPointers.delete(id);
    if (sliderPointer !== null && !seen.has(sliderPointer)) sliderPointer = null;

    updateBend(input.deltaTime, width, height);
    updateVoices(input.deltaTime);

    const canvas = sys.canvas;
    canvas.clear('#222831');
    canvas.save();
    canvas.scale(UI_SCALE, UI_SCALE);

    // Each voice: a circle where it sounds, sized by its volume, and a line
    // to its finger while it glides there.
    for (const voice of voices.values()) {
        const vx = voice.freq / MAX_FREQUENCY * width;
        const vy = playTop + (1 - voice.gain) * playHeight;
        if (voice.held) {
            const fx = voice.targetFreq / MAX_FREQUENCY * width;
            const fy = playTop + (1 - voice.targetGain) * playHeight;
            canvas.setStrokeColor(voice.color + '80');
            canvas.setStrokeWidth(2);
            canvas.drawLine(vx, vy, fx, fy);
            canvas.setFillColor('#eeeeee');
            canvas.drawText(`${(voice.targetFreq * bendFactor()).toFixed(0)} Hz`, fx + 14, fy - 14, 16);
        }
        canvas.setFillColor(voice.color);
        canvas.drawCircle(vx, vy, 6 + 30 * voice.gain);
    }

    drawPanel(width);
    canvas.restore();
    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
