// ═══════════════════════════════════════════════════════════════
//  THE GOLDEN DIAL — A 1950s Retro Radio Player
//  Synthesized stations with vintage radio aesthetics
// ═══════════════════════════════════════════════════════════════

// ---------- palette ----------
const COL_WOOD_DARK   = '#3B2516';
const COL_WOOD_MID    = '#5C3D2E';
const COL_WOOD_LIGHT  = '#6E4B37';
const COL_BRASS       = '#C9A96E';
const COL_BRASS_DARK  = '#8B7340';
const COL_IVORY       = '#F5E6C8';
const COL_IVORY_DIM   = '#D4C5A8';
const COL_DIAL_BG     = '#FFF8E7';
const COL_CLOTH       = '#A08060';
const COL_CLOTH_DARK  = '#7A6248';
const COL_INDICATOR   = '#FF3300';
const COL_GREEN_DIM   = '#225522';
const COL_GREEN_GLOW  = '#44FF44';
const COL_AMBER       = '#FFAA00';
const COL_BG          = '#1A1410';
const COL_TEXT_LIGHT  = '#F5DEB3';
const COL_TEXT_DIM    = '#A09080';

// ---------- station definitions ----------
const DIAL_MIN = 530;
const DIAL_MAX = 1600;
const STATION_LOCK = 25;   // kHz range for clean reception
const STATION_FADE = 55;   // kHz range where signal begins

const STATIONS = [
    {
        freq: 580,  name: 'Melody Lane',     desc: 'Golden Oldies & Classics',
        wave: 'triangle', bpm: 112,
        notes: [60,64,67,72, 71,67,64,60, 65,69,72,77, 76,72,69,65],
        bass:  [48,48,48,48, 48,48,48,48, 53,53,53,53, 53,53,53,53],
    },
    {
        freq: 770,  name: 'Jazz Cat FM',     desc: 'Smooth Jazz All Night',
        wave: 'sawtooth', bpm: 96,
        notes: [63,67,70,63, 65,68,72,65, 60,63,67,60, 62,65,70,62],
        bass:  [48,51,55,58, 53,56,60,63, 48,51,55,58, 50,53,57,60],
    },
    {
        freq: 940,  name: 'Classical Hour',  desc: 'Timeless Masterpieces',
        wave: 'sine', bpm: 132,
        notes: [69,72,76,81, 76,72,65,69, 60,64,67,72, 67,64,55,59],
        bass:  [57,57,57,57, 57,57,53,53, 48,48,48,48, 43,43,43,43],
    },
    {
        freq: 1100, name: 'News Wire',       desc: 'Around-The-Clock Reports',
        wave: 'square', bpm: 200,
        notes: [72,0,72,0,  72,72,0,0,   72,0,0,72,   0,0,72,0],
        bass:  [0,0,0,0,    0,0,0,0,     0,0,0,0,     0,0,0,0],
    },
    {
        freq: 1280, name: 'Night Sounds',    desc: 'Ambient & Relaxation',
        wave: 'sine', bpm: 40,
        notes: [60,60,62,62, 64,64,62,62, 60,60,59,59, 57,57,59,59],
        bass:  [36,36,36,36, 36,36,36,36, 36,36,36,36, 36,36,36,36],
    },
    {
        freq: 1450, name: 'Rock \'n\' Roll', desc: 'The Hits Keep On Coming',
        wave: 'square', bpm: 150,
        notes: [64,64,67,69, 71,71,69,67, 64,64,62,60, 62,62,64,64],
        bass:  [40,40,40,40, 45,45,45,45, 47,47,47,47, 45,45,45,45],
    },
];

// ---------- programme schedule (fake 1950s shows) ----------
const PROGRAMMES = [
    ['Melody Lane',     '"The Perry Carmichael Show"',      '8:00 PM'],
    ['Jazz Cat FM',     '"Miles After Midnight"',           '10:30 PM'],
    ['Classical Hour',  '"The Philharmonic Presents"',      '7:00 PM'],
    ['News Wire',       '"Evening World Dispatch"',         '6:00 PM'],
    ['Night Sounds',    '"Dreamland with Ed Sullivan"',     '11:00 PM'],
    ["Rock 'n' Roll",   '"American Bandstand Radio"',       '9:00 PM'],
];

// ---------- state ----------
let tuning      = 580;
let volume      = 0.55;
let power        = false;
let dragging     = null;   // 'dial' | 'volume' | null
let dragStartX   = 0;
let dragStartVal = 0;

let signalStrength = 0;
let currentStation = -1;
let prevStation    = -1;

let vuLevel   = 0;
let glowPhase = 0;
let powerAnim = 0;        // 0→1 warm-up

// audio handles
let noiseOsc   = -1;
let leadOsc    = -1;
let bassOsc    = -1;
let audioReady = false;

// ---------- helpers ----------
function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }
function lerp(a, b, t) { return a + (b - a) * t; }
function midiHz(n) { return sys.audio.midiToFreq(n); }

function nearestStation() {
    let best = -1, bestD = 9999;
    for (let i = 0; i < STATIONS.length; i++) {
        const d = Math.abs(tuning - STATIONS[i].freq);
        if (d < bestD) { bestD = d; best = i; }
    }
    return bestD <= STATION_FADE ? best : -1;
}

function stationSignal(idx) {
    if (idx < 0) return 0;
    const d = Math.abs(tuning - STATIONS[idx].freq);
    if (d <= STATION_LOCK) return 1;
    if (d >= STATION_FADE) return 0;
    return 1 - (d - STATION_LOCK) / (STATION_FADE - STATION_LOCK);
}

// ═══════════════════════════════════════════════════════════════
//  AUDIO
// ═══════════════════════════════════════════════════════════════

function initAudio() {
    sys.audio.start();
    sys.audio.setMasterGain(volume);

    noiseOsc = sys.audio.createOscillator();
    sys.audio.setOscillatorType(noiseOsc, sys.audio.NOISE);
    sys.audio.setOscillatorGain(noiseOsc, 0);
    sys.audio.startOscillator(noiseOsc);

    leadOsc = sys.audio.createOscillator();
    sys.audio.setOscillatorType(leadOsc, 'triangle');
    sys.audio.setOscillatorGain(leadOsc, 0);
    sys.audio.setOscillatorFrequency(leadOsc, 440);
    sys.audio.startOscillator(leadOsc);

    bassOsc = sys.audio.createOscillator();
    sys.audio.setOscillatorType(bassOsc, 'sine');
    sys.audio.setOscillatorGain(bassOsc, 0);
    sys.audio.setOscillatorFrequency(bassOsc, 110);
    sys.audio.startOscillator(bassOsc);

    audioReady = true;
}

function updateAudio(ts) {
    if (!audioReady) return;

    sys.audio.setMasterGain(power ? volume : 0);

    const sig = signalStrength * powerAnim;

    // static noise — louder when signal is weak, crackly when partial
    const noiseGain = power ? (sig > 0.8 ? 0.02 : (1 - sig) * 0.25) : 0;
    sys.audio.setOscillatorGain(noiseOsc, noiseGain);

    const idx = currentStation;
    if (idx < 0 || sig < 0.05) {
        sys.audio.setOscillatorGain(leadOsc, 0);
        sys.audio.setOscillatorGain(bassOsc, 0);
        return;
    }

    const st = STATIONS[idx];
    const seqLen = st.notes.length;
    const beatSec = 60 / st.bpm;
    const step = Math.floor((ts / 1000) / beatSec) % seqLen;

    // lead voice
    const note = st.notes[step];
    if (note > 0) {
        sys.audio.setOscillatorFrequency(leadOsc, midiHz(note));
        sys.audio.setOscillatorGain(leadOsc, 0.18 * sig);
    } else {
        sys.audio.setOscillatorGain(leadOsc, 0);
    }
    if (prevStation !== idx) {
        sys.audio.setOscillatorType(leadOsc, st.wave);
    }

    // bass voice
    const bn = st.bass[step];
    if (bn > 0) {
        sys.audio.setOscillatorFrequency(bassOsc, midiHz(bn));
        sys.audio.setOscillatorGain(bassOsc, 0.13 * sig);
    } else {
        sys.audio.setOscillatorGain(bassOsc, 0);
    }
}

// ═══════════════════════════════════════════════════════════════
//  INPUT
// ═══════════════════════════════════════════════════════════════

function handleInput(input, layout) {
    const m = input.mouse;
    const px = m.x, py = m.y;

    // power toggle (click release)
    if (m.leftPressed) {
        if (hitTest(px, py, layout.powerBtn)) {
            power = !power;
            if (power) powerAnim = 0;
            if (!audioReady && power) initAudio();
        }
    }

    // start drag
    if (m.leftPressed && !dragging) {
        if (hitTest(px, py, layout.dialArea)) {
            dragging = 'dial';
            dragStartX = px;
            dragStartVal = tuning;
        } else if (hitTest(px, py, layout.volKnob)) {
            dragging = 'volume';
            dragStartX = py;
            dragStartVal = volume;
        }
    }

    // drag
    if (m.left && dragging) {
        if (dragging === 'dial') {
            const dx = px - dragStartX;
            const range = layout.dialArea.w;
            tuning = clamp(dragStartVal + (dx / range) * (DIAL_MAX - DIAL_MIN), DIAL_MIN, DIAL_MAX);
        } else if (dragging === 'volume') {
            const dy = dragStartX - py;  // up = louder
            volume = clamp(dragStartVal + dy / 200, 0, 1);
        }
    }

    // release
    if (!m.left) dragging = null;

    // keyboard
    if (sys.input.isKeyDown(79))  tuning = clamp(tuning + 2, DIAL_MIN, DIAL_MAX); // right
    if (sys.input.isKeyDown(80))  tuning = clamp(tuning - 2, DIAL_MIN, DIAL_MAX); // left
    if (sys.input.isKeyDown(82))  volume = clamp(volume + 0.01, 0, 1);            // up
    if (sys.input.isKeyDown(81))  volume = clamp(volume - 0.01, 0, 1);            // down
    if (sys.input.isKeyPressed(44)) { power = !power; if (power) powerAnim = 0; if (!audioReady && power) initAudio(); } // space
}

function hitTest(x, y, r) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

// ═══════════════════════════════════════════════════════════════
//  DRAWING
// ═══════════════════════════════════════════════════════════════

function computeLayout(W, H) {
    const scale = Math.min(W / 600, H / 500);
    const rw = 560 * scale, rh = 440 * scale;
    const rx = (W - rw) / 2, ry = (H - rh) / 2;
    const pad = 20 * scale;

    const dialH = 70 * scale;
    const dialY = ry + pad + 30 * scale;

    const speakerY = dialY + dialH + 18 * scale;
    const speakerH = rh - (speakerY - ry) - 100 * scale;

    const knobRow = speakerY + speakerH + 12 * scale;

    return {
        scale, rx, ry, rw, rh, pad,
        dialArea:  { x: rx + pad, y: dialY, w: rw - pad * 2, h: dialH },
        speaker:   { x: rx + pad, y: speakerY, w: rw - pad * 2, h: speakerH },
        volKnob:   { x: rx + pad + 30 * scale, y: knobRow, w: 50 * scale, h: 50 * scale },
        powerBtn:  { x: rx + rw - pad - 80 * scale, y: knobRow, w: 50 * scale, h: 50 * scale },
        infoArea:  { x: rx + pad + 120 * scale, y: knobRow, w: rw - pad * 2 - 240 * scale, h: 50 * scale },
        vuMeter:   { x: rx + rw - pad - 150 * scale, y: dialY - 2 * scale, w: 120 * scale, h: 22 * scale },
    };
}

// ---- background & body ----
function drawBody(L) {
    const { rx, ry, rw, rh, scale } = L;

    // drop shadow
    sys.canvas.setFillColor('#000000');
    sys.canvas.setAlpha(80);
    sys.canvas.drawRoundRect(rx + 6 * scale, ry + 8 * scale, rw, rh, 18 * scale, 18 * scale);
    sys.canvas.setAlpha(255);

    // main body
    sys.canvas.setFillColor(COL_WOOD_MID);
    sys.canvas.drawRoundRect(rx, ry, rw, rh, 16 * scale, 16 * scale);

    // inner panel (darker)
    sys.canvas.setFillColor(COL_WOOD_DARK);
    sys.canvas.drawRoundRect(rx + 8 * scale, ry + 8 * scale, rw - 16 * scale, rh - 16 * scale, 12 * scale, 12 * scale);

    // wood grain lines
    sys.canvas.setStrokeColor(COL_WOOD_LIGHT);
    sys.canvas.setStrokeWidth(0.5 * scale);
    sys.canvas.setAlpha(40);
    for (let i = 0; i < 12; i++) {
        const gx = rx + 30 * scale + i * 44 * scale;
        sys.canvas.drawLine(gx, ry + 20 * scale, gx + 10 * scale, ry + rh - 20 * scale);
    }
    sys.canvas.setAlpha(255);

    // brass trim border
    sys.canvas.setStrokeColor(COL_BRASS);
    sys.canvas.setStrokeWidth(2.5 * scale);
    sys.canvas.drawRoundRect(rx + 4 * scale, ry + 4 * scale, rw - 8 * scale, rh - 8 * scale, 14 * scale, 14 * scale);

    // title
    sys.canvas.setFillColor(COL_BRASS);
    const title = 'THE GOLDEN DIAL';
    sys.canvas.drawText(title, rx + rw / 2 - 72 * scale, ry + 24 * scale, 14 * scale);
}

// ---- tuning dial ----
function drawDial(L) {
    const { dialArea, scale } = L;
    const { x, y, w, h } = dialArea;

    // dial background
    sys.canvas.setFillColor(COL_DIAL_BG);
    sys.canvas.drawRoundRect(x, y, w, h, 6 * scale, 6 * scale);

    // brass frame
    sys.canvas.setStrokeColor(COL_BRASS_DARK);
    sys.canvas.setStrokeWidth(2 * scale);
    sys.canvas.drawRoundRect(x, y, w, h, 6 * scale, 6 * scale);

    // frequency markings
    sys.canvas.setFillColor('#4A3020');
    sys.canvas.setStrokeColor('#4A3020');
    sys.canvas.setStrokeWidth(1 * scale);

    for (let f = 600; f <= 1600; f += 100) {
        const t = (f - DIAL_MIN) / (DIAL_MAX - DIAL_MIN);
        const mx = x + 12 * scale + t * (w - 24 * scale);
        const major = (f % 200 === 0);
        const tickH = major ? 16 * scale : 8 * scale;
        sys.canvas.drawLine(mx, y + h - 4 * scale, mx, y + h - 4 * scale - tickH);
        if (major) {
            sys.canvas.drawText(String(f), mx - 12 * scale, y + 16 * scale, 10 * scale);
        }
    }

    // station markers (small diamonds)
    for (let i = 0; i < STATIONS.length; i++) {
        const t = (STATIONS[i].freq - DIAL_MIN) / (DIAL_MAX - DIAL_MIN);
        const sx = x + 12 * scale + t * (w - 24 * scale);
        sys.canvas.setFillColor(COL_INDICATOR);
        sys.canvas.setAlpha(120);
        sys.canvas.drawCircle(sx, y + h / 2, 3 * scale);
        sys.canvas.setAlpha(255);
    }

    // tuning needle (red line)
    const nt = (tuning - DIAL_MIN) / (DIAL_MAX - DIAL_MIN);
    const nx = x + 12 * scale + nt * (w - 24 * scale);
    sys.canvas.setStrokeColor(COL_INDICATOR);
    sys.canvas.setStrokeWidth(2.5 * scale);
    sys.canvas.drawLine(nx, y + 4 * scale, nx, y + h - 4 * scale);

    // glow at needle tip
    sys.canvas.setFillColor(COL_INDICATOR);
    sys.canvas.setAlpha(Math.floor(140 + 60 * Math.sin(glowPhase * 4)));
    sys.canvas.drawCircle(nx, y + h / 2, 5 * scale);
    sys.canvas.setAlpha(255);

    // frequency readout
    sys.canvas.setFillColor('#2C1810');
    sys.canvas.drawText(Math.round(tuning) + ' kHz', x + w / 2 - 24 * scale, y + h - 8 * scale, 11 * scale);
}

// ---- speaker grille ----
function drawSpeaker(L) {
    const { speaker, scale } = L;
    const { x, y, w, h } = speaker;

    // cloth background
    sys.canvas.setFillColor(COL_CLOTH);
    sys.canvas.drawRoundRect(x, y, w, h, 8 * scale, 8 * scale);

    // grille slats (vertical lines)
    sys.canvas.setStrokeColor(COL_CLOTH_DARK);
    sys.canvas.setStrokeWidth(2 * scale);
    const slats = Math.floor(w / (8 * scale));
    for (let i = 0; i <= slats; i++) {
        const sx = x + 6 * scale + i * (w - 12 * scale) / slats;
        sys.canvas.drawLine(sx, y + 6 * scale, sx, y + h - 6 * scale);
    }

    // horizontal cross-bars
    sys.canvas.setStrokeWidth(1.5 * scale);
    const bars = Math.floor(h / (20 * scale));
    for (let i = 0; i <= bars; i++) {
        const sy = y + 6 * scale + i * (h - 12 * scale) / bars;
        sys.canvas.drawLine(x + 6 * scale, sy, x + w - 6 * scale, sy);
    }

    // brass frame
    sys.canvas.setStrokeColor(COL_BRASS);
    sys.canvas.setStrokeWidth(1.5 * scale);
    sys.canvas.drawRoundRect(x, y, w, h, 8 * scale, 8 * scale);

    // sound wave animation when playing
    if (signalStrength > 0.3 && power) {
        sys.canvas.setAlpha(Math.floor(signalStrength * 40));
        sys.canvas.setStrokeColor(COL_IVORY);
        sys.canvas.setStrokeWidth(1.5 * scale);
        const cx = x + w / 2;
        const cy = y + h / 2;
        for (let r = 1; r <= 3; r++) {
            const radius = (20 + r * 25) * scale + Math.sin(glowPhase * 3 + r) * 8 * scale;
            sys.canvas.drawArc(cx - radius, cy - radius, radius * 2, radius * 2, -30, 60, false);
            sys.canvas.drawArc(cx - radius, cy - radius, radius * 2, radius * 2, 150, 60, false);
        }
        sys.canvas.setAlpha(255);
    }
}

// ---- VU metre ----
function drawVUMeter(L) {
    const { vuMeter, scale } = L;
    const { x, y, w, h } = vuMeter;

    // background
    sys.canvas.setFillColor(COL_GREEN_DIM);
    sys.canvas.drawRoundRect(x, y, w, h, 3 * scale, 3 * scale);

    // level bar
    const barW = (w - 8 * scale) * vuLevel;
    if (barW > 1) {
        const col = vuLevel > 0.8 ? COL_INDICATOR : COL_GREEN_GLOW;
        sys.canvas.setFillColor(col);
        sys.canvas.drawRect(x + 4 * scale, y + 4 * scale, barW, h - 8 * scale);
    }

    // ticks
    sys.canvas.setStrokeColor(COL_IVORY);
    sys.canvas.setStrokeWidth(0.5 * scale);
    sys.canvas.setAlpha(100);
    for (let i = 1; i < 10; i++) {
        const tx = x + 4 * scale + i * (w - 8 * scale) / 10;
        sys.canvas.drawLine(tx, y + 2 * scale, tx, y + h - 2 * scale);
    }
    sys.canvas.setAlpha(255);

    // label
    sys.canvas.setFillColor(COL_IVORY);
    sys.canvas.drawText('VU', x + 4 * scale, y + h - 5 * scale, 8 * scale);

    // frame
    sys.canvas.setStrokeColor(COL_BRASS_DARK);
    sys.canvas.setStrokeWidth(1 * scale);
    sys.canvas.drawRoundRect(x, y, w, h, 3 * scale, 3 * scale);
}

// ---- knobs ----
function drawKnobs(L) {
    const { volKnob, powerBtn, scale } = L;

    // volume knob
    drawKnob(volKnob, volume, 'VOL', L);

    // power button
    const { x, y, w, h } = powerBtn;
    const cx = x + w / 2, cy = y + h / 2;
    const r = 18 * scale;

    // button base
    sys.canvas.setFillColor(COL_WOOD_LIGHT);
    sys.canvas.drawCircle(cx, cy, r + 3 * scale);
    sys.canvas.setFillColor(power ? '#443322' : '#332218');
    sys.canvas.drawCircle(cx, cy, r);
    sys.canvas.setStrokeColor(COL_BRASS);
    sys.canvas.setStrokeWidth(1.5 * scale);
    sys.canvas.drawCircle(cx, cy, r);

    // power indicator
    const glowStr = power ? (0.7 + 0.3 * Math.sin(glowPhase * 2)) * powerAnim : 0;
    if (glowStr > 0) {
        sys.canvas.setFillColor(COL_GREEN_GLOW);
        sys.canvas.setAlpha(Math.floor(glowStr * 200));
        sys.canvas.drawCircle(cx, cy, 6 * scale);
        sys.canvas.setAlpha(Math.floor(glowStr * 80));
        sys.canvas.drawCircle(cx, cy, 12 * scale);
        sys.canvas.setAlpha(255);
    }

    sys.canvas.setFillColor(COL_TEXT_DIM);
    sys.canvas.drawText('POWER', x + w / 2 - 18 * scale, y + h + 14 * scale, 9 * scale);
}

function drawKnob(area, val, label, L) {
    const { scale } = L;
    const cx = area.x + area.w / 2, cy = area.y + area.h / 2;
    const r = 20 * scale;

    // outer ring
    sys.canvas.setFillColor(COL_BRASS_DARK);
    sys.canvas.drawCircle(cx, cy, r + 4 * scale);
    // knob face
    sys.canvas.setFillColor(COL_WOOD_LIGHT);
    sys.canvas.drawCircle(cx, cy, r);
    sys.canvas.setFillColor(COL_WOOD_MID);
    sys.canvas.drawCircle(cx, cy, r - 3 * scale);
    // brass ring
    sys.canvas.setStrokeColor(COL_BRASS);
    sys.canvas.setStrokeWidth(1.5 * scale);
    sys.canvas.drawCircle(cx, cy, r);

    // pointer (rotated line based on val)
    const angle = -135 + val * 270;  // degrees
    const rad = angle * Math.PI / 180;
    const px = cx + Math.cos(rad) * (r - 6 * scale);
    const py = cy + Math.sin(rad) * (r - 6 * scale);
    sys.canvas.setStrokeColor(COL_IVORY);
    sys.canvas.setStrokeWidth(2.5 * scale);
    sys.canvas.drawLine(cx, cy, px, py);

    // label
    sys.canvas.setFillColor(COL_TEXT_DIM);
    sys.canvas.drawText(label, cx - 10 * scale, area.y + area.h + 14 * scale, 9 * scale);
}

// ---- station info panel ----
function drawInfoPanel(L) {
    const { infoArea, scale } = L;
    const { x, y, w, h } = infoArea;

    // panel background
    sys.canvas.setFillColor('#1E1208');
    sys.canvas.drawRoundRect(x, y, w, h, 4 * scale, 4 * scale);
    sys.canvas.setStrokeColor(COL_BRASS_DARK);
    sys.canvas.setStrokeWidth(1 * scale);
    sys.canvas.drawRoundRect(x, y, w, h, 4 * scale, 4 * scale);

    if (!power) {
        sys.canvas.setFillColor(COL_TEXT_DIM);
        sys.canvas.setAlpha(60);
        sys.canvas.drawText('- OFF -', x + w / 2 - 24 * scale, y + h / 2 + 4 * scale, 12 * scale);
        sys.canvas.setAlpha(255);
        return;
    }

    const idx = currentStation;
    if (idx >= 0 && signalStrength > 0.3) {
        // station name
        const alpha = Math.floor(clamp(signalStrength * 1.5, 0, 1) * 255);
        sys.canvas.setFillColor(COL_AMBER);
        sys.canvas.setAlpha(alpha);
        sys.canvas.drawText(STATIONS[idx].name, x + 10 * scale, y + 16 * scale, 13 * scale);

        // programme name
        sys.canvas.setFillColor(COL_TEXT_LIGHT);
        sys.canvas.setAlpha(Math.floor(alpha * 0.8));
        const prog = PROGRAMMES[idx];
        sys.canvas.drawText(prog[1], x + 10 * scale, y + 32 * scale, 10 * scale);
        sys.canvas.setAlpha(Math.floor(alpha * 0.5));
        sys.canvas.drawText(prog[2], x + w - 60 * scale, y + 16 * scale, 9 * scale);
        sys.canvas.setAlpha(255);
    } else {
        // scanning animation
        const dots = '.'.repeat(1 + Math.floor(glowPhase * 2) % 4);
        sys.canvas.setFillColor(COL_TEXT_DIM);
        sys.canvas.setAlpha(120);
        sys.canvas.drawText('Tuning' + dots, x + 10 * scale, y + h / 2 + 4 * scale, 11 * scale);
        sys.canvas.setAlpha(255);
    }
}

// ---- decorative elements ----
function drawDecorations(L) {
    const { rx, ry, rw, rh, scale } = L;

    // corner accents (small brass circles)
    sys.canvas.setFillColor(COL_BRASS);
    const inset = 18 * scale, cr = 4 * scale;
    sys.canvas.drawCircle(rx + inset, ry + inset, cr);
    sys.canvas.drawCircle(rx + rw - inset, ry + inset, cr);
    sys.canvas.drawCircle(rx + inset, ry + rh - inset, cr);
    sys.canvas.drawCircle(rx + rw - inset, ry + rh - inset, cr);

    // brass screws
    sys.canvas.setStrokeColor(COL_BRASS_DARK);
    sys.canvas.setStrokeWidth(1 * scale);
    sys.canvas.drawCircle(rx + inset, ry + inset, cr);
    sys.canvas.drawCircle(rx + rw - inset, ry + inset, cr);
    sys.canvas.drawLine(rx + inset - 2 * scale, ry + inset - 2 * scale,
                        rx + inset + 2 * scale, ry + inset + 2 * scale);
    sys.canvas.drawLine(rx + rw - inset - 2 * scale, ry + inset - 2 * scale,
                        rx + rw - inset + 2 * scale, ry + inset + 2 * scale);
}

// ---- help text ----
function drawHelp(W, H, L) {
    const { scale } = L;
    sys.canvas.setFillColor(COL_TEXT_DIM);
    sys.canvas.setAlpha(90);
    sys.canvas.drawText(
        '\u2190\u2192 Tune   \u2191\u2193 Volume   SPACE Power   Drag dial to tune',
        W / 2 - 180 * scale, H - 12 * scale, 10 * scale
    );
    sys.canvas.setAlpha(255);
}

// ---- signal strength dots ----
function drawSignalDots(L) {
    const { dialArea, scale } = L;
    const { x, y, w } = dialArea;
    const dotY = y - 14 * scale;
    const dotStart = x + w - 80 * scale;

    sys.canvas.setFillColor(COL_TEXT_DIM);
    sys.canvas.drawText('SIG', dotStart - 26 * scale, dotY + 6 * scale, 7 * scale);

    for (let i = 0; i < 5; i++) {
        const threshold = (i + 1) / 5;
        const on = signalStrength >= threshold && power;
        sys.canvas.setFillColor(on ? (i >= 4 ? COL_GREEN_GLOW : COL_AMBER) : '#333322');
        sys.canvas.drawCircle(dotStart + i * 14 * scale, dotY + 3 * scale, 4 * scale);
    }
}

// ═══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════

function frame(ts) {
    const W = sys.window.getWidth();
    const H = sys.window.getHeight();
    const input = sys.input.get();
    const dt = input.deltaTime || 0.016;

    const L = computeLayout(W, H);

    // input
    handleInput(input, L);

    // station tracking
    currentStation = nearestStation();
    const targetSig = currentStation >= 0 ? stationSignal(currentStation) : 0;
    signalStrength = lerp(signalStrength, targetSig, clamp(dt * 8, 0, 1));

    // power warm-up animation
    if (power && powerAnim < 1) {
        powerAnim = clamp(powerAnim + dt * 0.6, 0, 1);
    }

    // VU animation
    const vuTarget = power ? signalStrength * (0.4 + 0.6 * (0.5 + 0.5 * Math.sin(ts / 200))) * volume : 0;
    vuLevel = lerp(vuLevel, vuTarget, clamp(dt * 12, 0, 1));

    // glow phase
    glowPhase += dt;

    // audio
    updateAudio(ts);
    prevStation = currentStation;

    // ---- render ----

    // background
    sys.canvas.clear(COL_BG);

    // subtle vignette (dark rectangles at edges)
    sys.canvas.setFillColor('#000000');
    sys.canvas.setAlpha(40);
    sys.canvas.drawRect(0, 0, W, 30);
    sys.canvas.drawRect(0, H - 30, W, 30);
    sys.canvas.setAlpha(255);

    // radio
    drawBody(L);
    drawDial(L);
    drawSignalDots(L);
    drawVUMeter(L);
    drawSpeaker(L);
    drawKnobs(L);
    drawInfoPanel(L);
    drawDecorations(L);
    drawHelp(W, H, L);

    // warm CRT-like scanlines overlay (subtle)
    if (power && powerAnim > 0.5) {
        sys.canvas.setFillColor('#FFFFFF');
        sys.canvas.setAlpha(4);
        for (let sy = 0; sy < H; sy += 4) {
            sys.canvas.drawRect(0, sy, W, 1);
        }
        sys.canvas.setAlpha(255);
    }

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
