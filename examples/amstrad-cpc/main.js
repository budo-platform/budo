// =============================================================================
// Amstrad CPC6128 — BASIC 1.1 prompt emulation
// =============================================================================
//
// Boots into a recognisable Locomotive BASIC 1.1 ready screen (bright yellow
// on bright blue, the classic MODE 1 colour scheme), with a blinking square
// cursor, line editing, and a tiny BASIC-ish interpreter that recognises a
// handful of commands (CLS, MODE, INK, PEN, PAPER, BORDER, PRINT, LIST, NEW,
// RUN, HELP) plus stored numbered lines.
//
// A CRT shader with curvature, scanlines, slot mask, glow and vignette is
// applied on top of the canvas to give the look of a CTM-644 colour monitor.
// =============================================================================

// --- CPC palette (firmware ink numbers) --------------------------------------
//
// 27 official "hardware" colours indexed by firmware ink number. Approximate
// sRGB values calibrated against a CTM-644.
const CPC_COLOURS = [
    '#000000', // 0  Black
    '#000080', // 1  Blue
    '#0000FF', // 2  Bright Blue
    '#800000', // 3  Red
    '#800080', // 4  Magenta
    '#8000FF', // 5  Mauve
    '#FF0000', // 6  Bright Red
    '#FF0080', // 7  Purple
    '#FF00FF', // 8  Bright Magenta
    '#008000', // 9  Green
    '#008080', // 10 Cyan
    '#0080FF', // 11 Sky Blue
    '#808000', // 12 Yellow
    '#808080', // 13 White (pastel grey)
    '#8080FF', // 14 Pastel Blue
    '#FF8000', // 15 Orange
    '#FF8080', // 16 Pink
    '#FF80FF', // 17 Pastel Magenta
    '#00FF00', // 18 Bright Green
    '#00FF80', // 19 Sea Green
    '#00FFFF', // 20 Bright Cyan
    '#80FF00', // 21 Lime
    '#80FF80', // 22 Pastel Green
    '#80FFFF', // 23 Pastel Cyan
    '#FFFF00', // 24 Bright Yellow
    '#FFFF80', // 25 Pastel Yellow
    '#FFFFFF', // 26 Bright White
];

// --- Text screen (40×25, MODE 1) --------------------------------------------

const COLS = 40;
const ROWS = 25;

// Default MODE 1 ink table: 4 inks chosen by the firmware.
//   ink 0 = paper (blue)
//   ink 1 = pen   (bright yellow)
//   ink 2 = bright cyan
//   ink 3 = bright red
let inkTable = [1, 24, 20, 6];

let paperInk = 0;
let penInk   = 1;
let borderInk = 1;

// Two parallel buffers so each glyph keeps its own pen/paper.
const screen = new Array(ROWS * COLS).fill(' ');
const fg     = new Uint8Array(ROWS * COLS);
const bg     = new Uint8Array(ROWS * COLS);

let cx = 0, cy = 0;

function paperColor() { return CPC_COLOURS[inkTable[paperInk] || 0]; }
function penColor()   { return CPC_COLOURS[inkTable[penInk]   || 0]; }
function borderColor(){ return CPC_COLOURS[inkTable[borderInk] || 0]; }

function clearScreen() {
    for (let i = 0; i < screen.length; i++) {
        screen[i] = ' ';
        fg[i] = penInk;
        bg[i] = paperInk;
    }
    cx = 0; cy = 0;
}

function scrollUp() {
    for (let y = 0; y < ROWS - 1; y++) {
        for (let x = 0; x < COLS; x++) {
            const dst = y * COLS + x;
            const src = (y + 1) * COLS + x;
            screen[dst] = screen[src];
            fg[dst] = fg[src];
            bg[dst] = bg[src];
        }
    }
    for (let x = 0; x < COLS; x++) {
        const i = (ROWS - 1) * COLS + x;
        screen[i] = ' ';
        fg[i] = penInk;
        bg[i] = paperInk;
    }
}

function newline() {
    cx = 0;
    cy++;
    if (cy >= ROWS) { cy = ROWS - 1; scrollUp(); }
}

function putChar(ch) {
    if (ch === '\n') { newline(); return; }
    const i = cy * COLS + cx;
    screen[i] = ch;
    fg[i] = penInk;
    bg[i] = paperInk;
    cx++;
    if (cx >= COLS) newline();
}

function print(s) { for (let i = 0; i < s.length; i++) putChar(s[i]); }
function println(s) { print(s); newline(); }

// --- Boot banner -------------------------------------------------------------

function boot() {
    clearScreen();
    println('');
    println('Amstrad 128K Microcomputer  (v3)');
    println('');
    println('\x1F\x1F©1985 Amstrad Consumer Electronics plc'.replace(/\x1F/g, ' '));
    println('       and Locomotive Software Ltd.');
    println('');
    println('BASIC 1.1');
    println('');
    println('Ready');
}

// --- Tiny BASIC interpreter --------------------------------------------------

const program = new Map();   // line number → source string

function listProgram() {
    const keys = [...program.keys()].sort((a, b) => a - b);
    for (const k of keys) println(k + ' ' + program.get(k));
}

function runProgram() {
    const keys = [...program.keys()].sort((a, b) => a - b);
    for (const k of keys) executeStatement(program.get(k));
}

function parseInkArgs(rest) {
    return rest.split(',').map(s => parseInt(s.trim(), 10)).filter(n => !isNaN(n));
}

function executeStatement(line) {
    const src = line.trim();
    if (!src) return;

    const upper = src.toUpperCase();

    if (upper === 'CLS') { clearScreen(); return; }
    if (upper === 'NEW') { program.clear(); return; }
    if (upper === 'LIST') { listProgram(); return; }
    if (upper === 'RUN') { runProgram(); return; }
    if (upper === 'HELP' || upper === '?HELP') {
        println('Commands: CLS NEW LIST RUN MODE n');
        println('  BORDER n  PAPER n  PEN n');
        println('  INK p,c   PRINT "..."');
        return;
    }

    if (upper.startsWith('PRINT ') || upper.startsWith('?')) {
        const argStart = upper.startsWith('?') ? 1 : 6;
        let arg = src.slice(argStart).trim();
        if (arg.startsWith('"')) {
            const end = arg.indexOf('"', 1);
            if (end > 0) arg = arg.slice(1, end);
            else arg = arg.slice(1);
        } else {
            // Evaluate as a number-ish expression: only digits/operators allowed.
            if (/^[\d+\-*/.()\s]+$/.test(arg)) {
                try { arg = String(Function('"use strict";return (' + arg + ')')()); }
                catch (_) { arg = ''; }
            }
        }
        println(arg);
        return;
    }

    if (upper.startsWith('MODE ')) {
        // We only really support one mode visually, but accept the verb.
        clearScreen();
        return;
    }
    if (upper.startsWith('BORDER ')) {
        const n = parseInt(src.slice(7), 10);
        if (!isNaN(n)) borderInk = ((n % 27) + 27) % 27;
        return;
    }
    if (upper.startsWith('PAPER ')) {
        const n = parseInt(src.slice(6), 10);
        if (!isNaN(n)) paperInk = Math.max(0, Math.min(3, n));
        return;
    }
    if (upper.startsWith('PEN ')) {
        const n = parseInt(src.slice(4), 10);
        if (!isNaN(n)) penInk = Math.max(0, Math.min(3, n));
        return;
    }
    if (upper.startsWith('INK ')) {
        const args = parseInkArgs(src.slice(4));
        if (args.length >= 2) {
            const p = Math.max(0, Math.min(3, args[0]));
            const c = ((args[1] % 27) + 27) % 27;
            inkTable[p] = c;
        }
        return;
    }

    println('Syntax error');
}

function executeLine(line) {
    const src = line.trim();
    if (!src) return;

    // Numbered line — store or delete.
    const m = src.match(/^(\d+)\s*(.*)$/);
    if (m) {
        const num = parseInt(m[1], 10);
        const body = m[2];
        if (body === '') program.delete(num);
        else program.set(num, body);
        return;
    }

    executeStatement(src);
}

// --- Input line editor -------------------------------------------------------

let inputBuffer = '';
let inputStartX = 0;
let inputStartY = 0;

function showPrompt() {
    inputStartX = cx;
    inputStartY = cy;
    inputBuffer = '';
}

function redrawInput() {
    // Wipe input area then redraw current buffer.
    let x = inputStartX, y = inputStartY;
    const total = COLS * ROWS;
    let pos = y * COLS + x;
    while (pos < total) {
        screen[pos] = ' ';
        fg[pos] = penInk;
        bg[pos] = paperInk;
        pos++;
    }
    cx = inputStartX; cy = inputStartY;
    print(inputBuffer);
}

function commitLine() {
    const line = inputBuffer;
    newline();
    executeLine(line);
    println('Ready');
    showPrompt();
}

// --- Keyboard polling --------------------------------------------------------
//
// The runtime exposes scancode-level queries only. We map a useful US-layout
// subset to characters and implement key repeat ourselves.

const SC = {
    A: 4, Z: 29,
    ONE: 30, ZERO: 39,
    RETURN: 40, ESC: 41, BACKSPACE: 42, TAB: 43, SPACE: 44,
    MINUS: 45, EQUALS: 46, LBRACKET: 47, RBRACKET: 48, BACKSLASH: 49,
    SEMICOLON: 51, APOSTROPHE: 52, GRAVE: 53,
    COMMA: 54, PERIOD: 55, SLASH: 56,
    CAPS: 57,
    LEFT: 80, RIGHT: 79,
};

const SHIFT_DIGITS = ')!@#$%^&*(';     // index = digit value
const SHIFT_PUNCT  = {                  // unshifted → shifted
    '-': '_', '=': '+', '[': '{', ']': '}', '\\': '|',
    ';': ':', "'": '"', '`': '~', ',': '<', '.': '>', '/': '?',
};

function scancodeToChar(sc, shift) {
    if (sc >= SC.A && sc <= SC.Z) {
        const lower = String.fromCharCode('a'.charCodeAt(0) + (sc - SC.A));
        return shift ? lower.toUpperCase() : lower;
    }
    if (sc >= SC.ONE && sc <= SC.ZERO) {
        const digit = (sc === SC.ZERO) ? 0 : (sc - SC.ONE + 1);
        return shift ? SHIFT_DIGITS[digit] : String(digit);
    }
    if (sc === SC.SPACE) return ' ';
    const punct = {
        [SC.MINUS]: '-', [SC.EQUALS]: '=', [SC.LBRACKET]: '[',
        [SC.RBRACKET]: ']', [SC.BACKSLASH]: '\\', [SC.SEMICOLON]: ';',
        [SC.APOSTROPHE]: "'", [SC.GRAVE]: '`',
        [SC.COMMA]: ',', [SC.PERIOD]: '.', [SC.SLASH]: '/',
    }[sc];
    if (punct !== undefined) return shift ? (SHIFT_PUNCT[punct] || punct) : punct;
    return null;
}

// Watched scancodes for repeat handling.
const REPEATABLE = [
    SC.BACKSPACE, SC.SPACE,
    ...range(SC.A, SC.Z),
    ...range(SC.ONE, SC.ZERO),
    SC.MINUS, SC.EQUALS, SC.LBRACKET, SC.RBRACKET, SC.BACKSLASH,
    SC.SEMICOLON, SC.APOSTROPHE, SC.GRAVE,
    SC.COMMA, SC.PERIOD, SC.SLASH,
];
function range(a, b) { const r = []; for (let i = a; i <= b; i++) r.push(i); return r; }

const REPEAT_DELAY = 0.45;
const REPEAT_RATE  = 0.04;
const heldSince = new Map();
const nextRepeat = new Map();

function handleKey(sc, shift) {
    if (sc === SC.RETURN) { commitLine(); return; }
    if (sc === SC.BACKSPACE) {
        if (inputBuffer.length > 0) {
            inputBuffer = inputBuffer.slice(0, -1);
            redrawInput();
        }
        return;
    }
    const ch = scancodeToChar(sc, shift);
    if (ch !== null) {
        inputBuffer += ch;
        redrawInput();
    }
}

function pollKeyboard(input, dt) {
    const shift = input.keyboard.shift;

    // One-shot keys (fire on press regardless of repeat).
    if (sys.input.isKeyPressed(SC.RETURN)) handleKey(SC.RETURN, shift);

    // Repeatable keys.
    for (const sc of REPEATABLE) {
        const down = sys.input.isKeyDown(sc);
        const pressed = sys.input.isKeyPressed(sc);
        if (pressed) {
            handleKey(sc, shift);
            heldSince.set(sc, 0);
            nextRepeat.set(sc, REPEAT_DELAY);
        } else if (down) {
            const t = (heldSince.get(sc) || 0) + dt;
            heldSince.set(sc, t);
            const next = nextRepeat.get(sc) || REPEAT_DELAY;
            if (t >= next) {
                handleKey(sc, shift);
                nextRepeat.set(sc, next + REPEAT_RATE);
            }
        } else {
            if (heldSince.has(sc)) { heldSince.delete(sc); nextRepeat.delete(sc); }
        }
    }
}

// --- Rendering ---------------------------------------------------------------

const shaderProgram = sys.gl.createProgram('shader.vert', 'shader.frag');

let cursorBlinkT = 0;
let cursorOn = true;
let totalTime = 0;

function drawScreen() {
    const W = sys.window.getWidth();
    const H = sys.window.getHeight();

    // Fit a 40×25 character grid centred inside a border, preserving aspect.
    const borderRatio = 0.06;
    const innerW = W * (1 - borderRatio * 2);
    const innerH = H * (1 - borderRatio * 2);

    // CPC pixel aspect in MODE 1 is roughly 0.5:1 per char, so 40×25 with
    // square chars maps to a 4:3 canvas when char width = char height * 0.6.
    const charH = Math.min(innerH / ROWS, (innerW / COLS) / 0.6);
    const charW = charH * 0.6;

    const gridW = charW * COLS;
    const gridH = charH * ROWS;
    const ox = (W - gridW) / 2;
    const oy = (H - gridH) / 2;

    // Border (paper of border ink).
    sys.canvas.clear(borderColor());

    // Paper background of the text area.
    sys.canvas.setFillColor(paperColor());
    sys.canvas.drawRect(ox, oy, gridW, gridH);

    // Per-cell background (only where bg differs from paper).
    for (let y = 0; y < ROWS; y++) {
        for (let x = 0; x < COLS; x++) {
            const i = y * COLS + x;
            if (bg[i] !== paperInk) {
                sys.canvas.setFillColor(CPC_COLOURS[inkTable[bg[i]] || 0]);
                sys.canvas.drawRect(ox + x * charW, oy + y * charH, charW + 1, charH + 1);
            }
        }
    }

    // Glyphs.
    const fontSize = charH * 0.95;
    for (let y = 0; y < ROWS; y++) {
        for (let x = 0; x < COLS; x++) {
            const i = y * COLS + x;
            const ch = screen[i];
            if (ch === ' ') continue;
            sys.canvas.setFillColor(CPC_COLOURS[inkTable[fg[i]] || 0]);
            sys.canvas.drawText(ch, ox + x * charW + charW * 0.1, oy + y * charH + charH * 0.85, fontSize);
        }
    }

    // Blinking square cursor (CPC firmware style).
    if (cursorOn) {
        sys.canvas.setFillColor(penColor());
        sys.canvas.drawRect(ox + cx * charW, oy + cy * charH, charW, charH);
        // Show the character beneath in paper colour, if any.
        const i = cy * COLS + cx;
        const ch = screen[i];
        if (ch && ch !== ' ') {
            sys.canvas.setFillColor(paperColor());
            sys.canvas.drawText(ch, ox + cx * charW + charW * 0.1, oy + cy * charH + charH * 0.85, fontSize);
        }
    }
}

// --- Main loop ---------------------------------------------------------------

boot();
showPrompt();

function frame(timestamp) {
    const input = sys.input.get();
    const dt = input.deltaTime || 0;
    totalTime += dt;

    pollKeyboard(input, dt);

    cursorBlinkT += dt;
    if (cursorBlinkT >= 0.4) { cursorBlinkT = 0; cursorOn = !cursorOn; }

    drawScreen();

    sys.gl.setUniform1f(shaderProgram, 'u_scanline_intensity', 0.30);
    sys.gl.setUniform1f(shaderProgram, 'u_aberration', 1.0);
    sys.gl.setUniform1f(shaderProgram, 'u_curvature', 0.18);
    sys.gl.setUniform1f(shaderProgram, 'u_glow', 0.18);
    sys.gl.bindScreen();
    sys.gl.drawFullscreen(shaderProgram);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
