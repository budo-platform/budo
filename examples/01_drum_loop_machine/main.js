/**
 * Drum Loops — a 16-step drum machine for Budo.
 *
 * Features:
 *  - 6 drum tracks (Kick, Snare, Closed HH, Open HH, Clap, Tom)
 *  - 16 step sequencer with visual playhead
 *  - Tap any cell to toggle a beat
 *  - Tempo control (40–220 BPM)
 *  - Master volume slider
 *  - Per-track mute toggle
 *  - Play / Stop / Clear / Random
 *  - Sound source: local Creative Commons drum samples loaded with
 *    Budo's audio buffer asset API.
 */

// ───────────────────────────────────────────────
//  CONFIG
// ───────────────────────────────────────────────

const TRACKS = [
    { id: 'kick', name: 'KICK', color: '#FF5C5C' },
    { id: 'snare', name: 'SNARE', color: '#FFB347' },
    { id: 'chh', name: 'C-HAT', color: '#FFE066' },
    { id: 'ohh', name: 'O-HAT', color: '#9CFF6B' },
    { id: 'clap', name: 'CLAP', color: '#6BD4FF' },
    { id: 'tom', name: 'TOM', color: '#C58BFF' },
];

const NUM_STEPS = 16;

// Local sample kits. Files are from Virtuosity Drums (CC0 1.0) and are
// stored under samples/virtuosity. See samples/README.md for source notes.
function kitSamples(folder) {
    return {
        kick: `samples/virtuosity/${folder}/kick.flac`,
        snare: `samples/virtuosity/${folder}/snare.flac`,
        chh: `samples/virtuosity/${folder}/chh.flac`,
        ohh: `samples/virtuosity/${folder}/ohh.flac`,
        clap: `samples/virtuosity/${folder}/clap.wav`,
        tom: `samples/virtuosity/${folder}/tom.flac`,
    };
}

const SAMPLE_KITS = [
    { name: 'Lofi', samples: kitSamples('lofi') },
    { name: 'Mid', samples: kitSamples('mid') },
    { name: 'OH', samples: kitSamples('oh') },
    { name: 'Room', samples: kitSamples('room') },
    { name: 'Kick', samples: kitSamples('kick') },
    { name: 'Snare', samples: kitSamples('snare') },
];

// ───────────────────────────────────────────────
//  STATE
// ───────────────────────────────────────────────

// pattern[trackIdx][stepIdx] = bool
const pattern = TRACKS.map(() => new Array(NUM_STEPS).fill(false));
const muted = TRACKS.map(() => false);

// Some starter beats so it makes a sound on first launch.
function seedDefaultPattern() {
    for (let i = 0; i < NUM_STEPS; i++) pattern[0][i] = (i % 4 === 0);                    // kick
    for (let i = 0; i < NUM_STEPS; i++) pattern[1][i] = (i % 8 === 4);                    // snare backbeat
    for (let i = 0; i < NUM_STEPS; i++) pattern[2][i] = (i % 2 === 0);                    // closed hat
    pattern[3][14] = true;                                                                 // open hat
    pattern[4][12] = true;                                                                 // clap
}
seedDefaultPattern();

let bpm = 110;
let masterGain = 0.7;
let playing = false;
let currentStep = -1;
let stepStartMs = 0;
let kitIndex = 0;
let kitStatus = 'Loading samples...';

// Sample buffers per track for each local kit.
let activeBuffers = {};
const loadedKitBuffers = [];

// Active playback handles, used so we can stop a hat when an open-hat hits.
const activePlaybackByTrack = {};

// SDL scancodes
const SDL_SPACE = 44, SDL_C = 6, SDL_R = 21, SDL_LEFT = 80, SDL_RIGHT = 79, SDL_UP = 82, SDL_DOWN = 81, SDL_K = 14;

// ───────────────────────────────────────────────
//  SAMPLE LOADING
// ───────────────────────────────────────────────

function loadKit(kitIdx) {
    if (kitIdx < 0 || kitIdx >= SAMPLE_KITS.length) return;
    if (loadedKitBuffers[kitIdx]) {
        activeBuffers = loadedKitBuffers[kitIdx];
        kitIndex = kitIdx;
        kitStatus = `${SAMPLE_KITS[kitIdx].name} ready`;
        return;
    }

    const kit = SAMPLE_KITS[kitIdx];
    const buffers = {};
    let failures = 0;

    for (const trk of TRACKS) {
        const path = kit.samples[trk.id];
        const id = sys.audio.loadBuffer(path);
        if (id < 0) {
            sys.log(`Could not load ${path}: ${sys.audio.getError()}`);
            failures++;
        }
        buffers[trk.id] = id;
    }

    loadedKitBuffers[kitIdx] = buffers;
    activeBuffers = buffers;
    kitIndex = kitIdx;
    kitStatus = failures === 0
        ? `${kit.name} ready`
        : `${kit.name} ready (${failures} missing)`;
}

// ───────────────────────────────────────────────
//  PLAYBACK
// ───────────────────────────────────────────────

function triggerTrack(trackIdx) {
    if (muted[trackIdx]) return;
    const trk = TRACKS[trackIdx];
    const bufId = activeBuffers[trk.id];
    if (bufId == null) return;

    // For hi-hats: closed hi-hat chokes open hi-hat, mimicking a real kit.
    if (trk.id === 'chh' && activePlaybackByTrack['ohh']) {
        sys.audio.stopBuffer(activePlaybackByTrack['ohh']);
        activePlaybackByTrack['ohh'] = 0;
    }

    // Stop the previous voice for this track to keep things tight.
    if (activePlaybackByTrack[trk.id]) {
        sys.audio.stopBuffer(activePlaybackByTrack[trk.id]);
    }
    activePlaybackByTrack[trk.id] = sys.audio.playBuffer(bufId, false, 0.9);
}

function stepDurationMs() { return (60 / bpm) * 1000 / 4; }

function advanceSequencer(now) {
    if (!playing) return;
    const dur = stepDurationMs();
    while (now - stepStartMs >= dur) {
        currentStep = (currentStep + 1) % NUM_STEPS;
        stepStartMs += dur;
        for (let t = 0; t < TRACKS.length; t++) {
            if (pattern[t][currentStep]) triggerTrack(t);
        }
    }
}

function startPlay(now) {
    playing = true;
    currentStep = -1;
    stepStartMs = now - stepDurationMs(); // trigger step 0 immediately
}

function stopPlay() {
    playing = false;
    currentStep = -1;
}

// ───────────────────────────────────────────────
//  INPUT
// ───────────────────────────────────────────────

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function hit(x, y, r) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; }

const pointerState = new Map(); // pointerId -> { mode, lastCell }

function clearPattern() {
    for (let t = 0; t < TRACKS.length; t++)
        for (let s = 0; s < NUM_STEPS; s++) pattern[t][s] = false;
}

function randomPattern() {
    const densities = [0.30, 0.20, 0.55, 0.10, 0.10, 0.12];
    for (let t = 0; t < TRACKS.length; t++)
        for (let s = 0; s < NUM_STEPS; s++) pattern[t][s] = Math.random() < densities[t];
}

function handleInput(input, layout, now) {
    const m = input.mouse;
    const mx = m.x, my = m.y;

    // ── Buttons (transport, kit) on mouse down ──
    if (m.leftPressed) {
        if (hit(mx, my, layout.playBtn)) {
            playing ? stopPlay() : startPlay(now);
            return;
        }
        if (hit(mx, my, layout.clearBtn)) { clearPattern(); return; }
        if (hit(mx, my, layout.randomBtn)) { randomPattern(); return; }
        if (hit(mx, my, layout.bpmDown)) { bpm = clamp(bpm - 1, 40, 220); return; }
        if (hit(mx, my, layout.bpmUp)) { bpm = clamp(bpm + 1, 40, 220); return; }

        for (let i = 0; i < layout.kitButtons.length; i++) {
            if (hit(mx, my, layout.kitButtons[i])) {
                loadKit(layout.kitButtons[i].idx);
                return;
            }
        }
    }

    // ── Multi-pointer for grid + mute ──
    const activeIds = new Set();
    for (const p of input.pointers) {
        activeIds.add(p.id);
        const st = pointerState.get(p.id) || { mode: null, lastCell: null, paintValue: false };
        if (p.pressed) {
            // Mute toggles
            const mIdx = hitTrackLabel(p.x, p.y, layout);
            if (mIdx >= 0) {
                muted[mIdx] = !muted[mIdx];
                st.mode = 'mute';
                pointerState.set(p.id, st);
                continue;
            }
            // Volume slider
            if (hit(p.x, p.y, layout.volTrack)) {
                masterGain = clamp((p.x - layout.volTrack.x) / layout.volTrack.w, 0, 1);
                sys.audio.setMasterGain(masterGain);
                st.mode = 'volume';
                pointerState.set(p.id, st);
                continue;
            }
            // Grid paint
            const cell = hitGridCell(p.x, p.y, layout);
            if (cell) {
                const cur = pattern[cell.t][cell.s];
                pattern[cell.t][cell.s] = !cur;
                st.mode = 'paint';
                st.paintValue = !cur;
                st.lastCell = `${cell.t}:${cell.s}`;
                pointerState.set(p.id, st);
                continue;
            }
        } else if (p.down) {
            if (st.mode === 'volume') {
                masterGain = clamp((p.x - layout.volTrack.x) / layout.volTrack.w, 0, 1);
                sys.audio.setMasterGain(masterGain);
            } else if (st.mode === 'paint') {
                const cell = hitGridCell(p.x, p.y, layout);
                if (cell) {
                    const key = `${cell.t}:${cell.s}`;
                    if (key !== st.lastCell) {
                        pattern[cell.t][cell.s] = st.paintValue;
                        st.lastCell = key;
                    }
                }
            }
        }
    }
    for (const id of Array.from(pointerState.keys())) {
        if (!activeIds.has(id)) pointerState.delete(id);
    }

    // ── Keyboard shortcuts ──
    if (sys.input.isKeyPressed(SDL_SPACE)) playing ? stopPlay() : startPlay(now);
    if (sys.input.isKeyPressed(SDL_C)) clearPattern();
    if (sys.input.isKeyPressed(SDL_R)) randomPattern();
    if (sys.input.isKeyDown(SDL_LEFT)) bpm = clamp(bpm - 1, 40, 220);
    if (sys.input.isKeyDown(SDL_RIGHT)) bpm = clamp(bpm + 1, 40, 220);
    if (sys.input.isKeyDown(SDL_UP)) { masterGain = clamp(masterGain + 0.01, 0, 1); sys.audio.setMasterGain(masterGain); }
    if (sys.input.isKeyDown(SDL_DOWN)) { masterGain = clamp(masterGain - 0.01, 0, 1); sys.audio.setMasterGain(masterGain); }
}

function hitGridCell(x, y, L) {
    if (!hit(x, y, L.gridArea)) return null;
    const s = Math.floor((x - L.gridArea.x) / L.cellW);
    const t = Math.floor((y - L.gridArea.y) / L.cellH);
    if (s < 0 || s >= NUM_STEPS || t < 0 || t >= TRACKS.length) return null;
    return { t, s };
}

function hitTrackLabel(x, y, L) {
    for (let t = 0; t < TRACKS.length; t++) {
        const r = { x: L.labelX, y: L.gridArea.y + t * L.cellH, w: L.labelW, h: L.cellH };
        if (hit(x, y, r)) return t;
    }
    return -1;
}

// ───────────────────────────────────────────────
//  LAYOUT
// ───────────────────────────────────────────────

function computeLayout(W, H) {
    const headerH = clamp(H * 0.10, 56, 96);
    const transportH = clamp(H * 0.10, 60, 96);
    const footerH = clamp(H * 0.10, 60, 96);
    const padX = clamp(W * 0.025, 12, 28);

    const labelW = clamp(W * 0.10, 70, 110);
    const gridX = padX + labelW + 8;
    const gridY = headerH + transportH + 6;
    const gridW = W - gridX - padX;
    const gridH = H - footerH - gridY - 8;
    const cellW = gridW / NUM_STEPS;
    const cellH = gridH / TRACKS.length;

    // Transport row
    const trY = headerH + 8;
    const trH = transportH - 16;
    const btnW = clamp(W * 0.12, 90, 140);
    const playBtn = { x: padX, y: trY, w: btnW, h: trH };
    const clearBtn = { x: padX + (btnW + 8), y: trY, w: btnW * 0.8, h: trH };
    const randomBtn = { x: padX + (btnW + 8) + btnW * 0.8 + 8, y: trY, w: btnW * 0.9, h: trH };

    // BPM controls (right side of transport)
    const bpmBlockW = clamp(W * 0.25, 200, 320);
    const bpmX = W - padX - bpmBlockW;
    const bpmDown = { x: bpmX, y: trY, w: trH, h: trH };
    const bpmUp = { x: bpmX + bpmBlockW - trH, y: trY, w: trH, h: trH };

    // Kit selector below transport (above grid). Reserve none — put kits in footer.

    // Footer: volume + kit buttons + status
    const ftY = H - footerH + 8;
    const ftH = footerH - 16;
    let volTrackW = clamp(W * 0.23, 120, 260);
    const volValueW = clamp(W * 0.07, 42, 58);
    const kitGap = 6;
    const minKitBtnW = 44;
    const minKitCols = Math.min(3, SAMPLE_KITS.length);
    const minKitAreaW = minKitCols * minKitBtnW + (minKitCols - 1) * kitGap;
    const kitInset = 22;
    const firstKitX = padX + volTrackW + volValueW + kitInset;
    const kitDeficit = minKitAreaW - (W - firstKitX - padX);
    if (kitDeficit > 0) {
        volTrackW = Math.max(92, volTrackW - kitDeficit);
    }
    const volTrack = { x: padX, y: ftY + ftH * 0.35, w: volTrackW, h: ftH * 0.30 };
    const volValue = { x: volTrack.x + volTrack.w + 8, y: ftY, w: volValueW, h: ftH };

    const kitButtons = [];
    const kitX0 = volValue.x + volValue.w + kitInset;
    const kitCount = SAMPLE_KITS.length;
    const availableKitW = Math.max(1, W - kitX0 - padX);
    const singleRowW = (availableKitW - kitGap * (kitCount - 1)) / kitCount;
    const kitRows = singleRowW < 64 && kitCount > 3 ? 2 : 1;
    const kitCols = Math.ceil(kitCount / kitRows);
    const kitBtnW = clamp((availableKitW - kitGap * (kitCols - 1)) / kitCols, 44, 140);
    const kitBtnH = kitRows === 1 ? ftH : (ftH - kitGap) / 2;
    for (let i = 0; i < SAMPLE_KITS.length; i++) {
        const kitRow = Math.floor(i / kitCols);
        const kitCol = i % kitCols;
        kitButtons.push({
            x: kitX0 + kitCol * (kitBtnW + kitGap),
            y: ftY + kitRow * (kitBtnH + kitGap),
            w: kitBtnW,
            h: kitBtnH,
            label: SAMPLE_KITS[i].name.toUpperCase(),
            idx: i,
        });
    }

    return {
        W, H, padX,
        headerH, transportH, footerH,
        labelX: padX, labelW,
        gridArea: { x: gridX, y: gridY, w: gridW, h: gridH },
        cellW, cellH,
        playBtn, clearBtn, randomBtn,
        bpmDown, bpmUp,
        bpmLabel: { x: bpmX + trH + 8, y: trY, w: bpmBlockW - trH * 2 - 16, h: trH },
        volTrack,
        volValue,
        kitButtons,
    };
}

// ───────────────────────────────────────────────
//  DRAWING
// ───────────────────────────────────────────────

const COL_BG = '#15161C';
const COL_PANEL = '#1F2230';
const COL_PANEL_HI = '#272B3D';
const COL_GRID_BG = '#11131A';
const COL_CELL_OFF = '#2A2E40';
const COL_CELL_BEAT = '#34394F';
const COL_BAR_LINE = '#3D4359';
const COL_TEXT = '#E6E8F0';
const COL_TEXT_DIM = '#8C92AB';
const COL_ACCENT = '#FF7A5C';
const COL_PLAY = '#5CCB7A';
const COL_PLAYHEAD = '#FFE066';
const COL_BTN = '#2A2E40';
const COL_BTN_HI = '#3A4060';
const COL_MUTED = '#3A2030';

function drawText(text, x, y, size, color) {
    sys.canvas.setFillColor(color);
    sys.canvas.drawText(text, x, y, size);
}

function drawTextCentered(text, cx, cy, size, color) {
    const w = sys.canvas.measureText(text, size);
    sys.canvas.setFillColor(color);
    sys.canvas.drawText(text, cx - w * 0.5, cy + size * 0.35, size);
}

function drawButton(r, label, color, textColor, fontSize) {
    sys.canvas.setFillColor(color);
    sys.canvas.drawRoundRect(r.x, r.y, r.w, r.h, 8, 8);
    drawTextCentered(label, r.x + r.w * 0.5, r.y + r.h * 0.5, fontSize, textColor);
}

function drawButtonFit(r, label, color, textColor, maxFontSize, minFontSize) {
    let fontSize = maxFontSize;
    while (fontSize > minFontSize && sys.canvas.measureText(label, fontSize) > r.w - 10) {
        fontSize -= 1;
    }
    drawButton(r, label, color, textColor, fontSize);
}

function drawHeader(L) {
    sys.canvas.setFillColor(COL_PANEL);
    sys.canvas.drawRect(0, 0, L.W, L.headerH);

    const titleSize = clamp(L.W * 0.030, 22, 34);
    drawText('DRUM LOOPS', L.padX, L.headerH * 0.55, titleSize, COL_TEXT);

    const subtitleSize = clamp(L.W * 0.014, 11, 16);
    drawText(`${kitStatus}  ·  Space play  ·  C clear  ·  R random  ·  ←/→ tempo`,
        L.padX, L.headerH * 0.85, subtitleSize, COL_TEXT_DIM);
}

function drawTransport(L) {
    drawButton(L.playBtn, playing ? 'STOP' : 'PLAY',
        playing ? COL_ACCENT : COL_PLAY, '#0E0E12', clamp(L.W * 0.022, 16, 22));
    drawButton(L.clearBtn, 'CLEAR', COL_BTN, COL_TEXT, clamp(L.W * 0.018, 14, 18));
    drawButton(L.randomBtn, 'RANDOM', COL_BTN, COL_TEXT, clamp(L.W * 0.018, 14, 18));

    drawButton(L.bpmDown, '–', COL_BTN, COL_TEXT, clamp(L.W * 0.026, 18, 26));
    drawButton(L.bpmUp, '+', COL_BTN, COL_TEXT, clamp(L.W * 0.026, 18, 26));
    sys.canvas.setFillColor(COL_PANEL_HI);
    sys.canvas.drawRoundRect(L.bpmLabel.x, L.bpmLabel.y, L.bpmLabel.w, L.bpmLabel.h, 8, 8);
    drawTextCentered(`${bpm} BPM`,
        L.bpmLabel.x + L.bpmLabel.w * 0.5,
        L.bpmLabel.y + L.bpmLabel.h * 0.5,
        clamp(L.W * 0.022, 16, 24), COL_TEXT);
}

function drawGrid(L) {
    // grid background
    sys.canvas.setFillColor(COL_GRID_BG);
    sys.canvas.drawRoundRect(L.gridArea.x - 4, L.gridArea.y - 4, L.gridArea.w + 8, L.gridArea.h + 8, 10, 10);

    const labelSize = clamp(L.cellH * 0.40, 12, 22);

    for (let t = 0; t < TRACKS.length; t++) {
        const trk = TRACKS[t];
        const rowY = L.gridArea.y + t * L.cellH;

        // Track label / mute button
        const lblBg = muted[t] ? COL_MUTED : COL_PANEL;
        sys.canvas.setFillColor(lblBg);
        sys.canvas.drawRoundRect(L.labelX, rowY + 2, L.labelW, L.cellH - 4, 8, 8);
        sys.canvas.setFillColor(trk.color);
        sys.canvas.drawCircle(L.labelX + 12, rowY + L.cellH * 0.5, 5);
        drawText(trk.name, L.labelX + 24, rowY + L.cellH * 0.62, labelSize, muted[t] ? COL_TEXT_DIM : COL_TEXT);
        if (muted[t]) {
            drawText('MUTED', L.labelX + 24, rowY + L.cellH * 0.85, labelSize * 0.55, COL_ACCENT);
        }

        for (let s = 0; s < NUM_STEPS; s++) {
            const cx = L.gridArea.x + s * L.cellW;
            const cy = rowY;
            const isBar = (Math.floor(s / 4) % 2 === 0);
            const cellBg = pattern[t][s] ? trk.color : (isBar ? COL_CELL_BEAT : COL_CELL_OFF);

            // dim if muted
            if (muted[t] && pattern[t][s]) {
                sys.canvas.setFillColor(trk.color);
                sys.canvas.setAlpha(80);
                sys.canvas.drawRoundRect(cx + 2, cy + 2, L.cellW - 4, L.cellH - 4, 5, 5);
                sys.canvas.setAlpha(255);
            } else {
                sys.canvas.setFillColor(cellBg);
                sys.canvas.drawRoundRect(cx + 2, cy + 2, L.cellW - 4, L.cellH - 4, 5, 5);
            }

            // playhead highlight
            if (s === currentStep && playing) {
                sys.canvas.setStrokeColor(COL_PLAYHEAD);
                sys.canvas.setStrokeWidth(2);
                sys.canvas.drawRoundRect(cx + 1, cy + 1, L.cellW - 2, L.cellH - 2, 6, 6);
            }
        }
    }

    // Bar separators (every 4 steps)
    sys.canvas.setStrokeColor(COL_BAR_LINE);
    sys.canvas.setStrokeWidth(1);
    for (let s = 4; s < NUM_STEPS; s += 4) {
        const x = L.gridArea.x + s * L.cellW;
        sys.canvas.drawLine(x, L.gridArea.y, x, L.gridArea.y + L.gridArea.h);
    }
}

function drawFooter(L) {
    sys.canvas.setFillColor(COL_PANEL);
    sys.canvas.drawRect(0, L.H - L.footerH, L.W, L.footerH);

    // Volume label + slider
    const vt = L.volTrack;
    const labelSize = clamp(L.W * 0.014, 11, 16);
    drawText('VOLUME', L.padX, vt.y - 8, labelSize, COL_TEXT_DIM);
    sys.canvas.setFillColor(COL_PANEL_HI);
    sys.canvas.drawRoundRect(vt.x, vt.y, vt.w, vt.h, vt.h * 0.5, vt.h * 0.5);
    sys.canvas.setFillColor(COL_PLAY);
    sys.canvas.drawRoundRect(vt.x, vt.y, vt.w * masterGain, vt.h, vt.h * 0.5, vt.h * 0.5);
    sys.canvas.setFillColor(COL_TEXT);
    sys.canvas.drawCircle(vt.x + vt.w * masterGain, vt.y + vt.h * 0.5, vt.h * 0.7);
    drawText(`${Math.round(masterGain * 100)}%`, L.volValue.x, vt.y + vt.h * 0.8, labelSize, COL_TEXT);

    // Kit buttons
    if (L.kitButtons.length > 0) {
        drawText('KIT', L.kitButtons[0].x, L.kitButtons[0].y - 8, labelSize, COL_TEXT_DIM);
        for (const kb of L.kitButtons) {
            const isActive = (kb.idx === kitIndex);
            const bg = isActive ? COL_BTN_HI : COL_BTN;
            const txt = isActive ? COL_PLAYHEAD : COL_TEXT;
            drawButtonFit(kb, kb.label, bg, txt, clamp(L.W * 0.014, 11, 16), 9);
        }
    }
}

// ───────────────────────────────────────────────
//  FRAME
// ───────────────────────────────────────────────

function frame(timestamp) {
    const W = sys.window.getWidth();
    const H = sys.window.getHeight();
    const L = computeLayout(W, H);

    const input = sys.input.get();
    handleInput(input, L, timestamp);

    advanceSequencer(timestamp);

    sys.canvas.clear(COL_BG);
    drawHeader(L);
    drawTransport(L);
    drawGrid(L);
    drawFooter(L);

    sys.animation.requestFrame(frame);
}

// ───────────────────────────────────────────────
//  BOOT
// ───────────────────────────────────────────────

sys.audio.start();
sys.audio.setMasterGain(masterGain);
loadKit(0);

sys.animation.requestFrame(frame);
