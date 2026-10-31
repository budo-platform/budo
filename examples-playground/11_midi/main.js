/**
 * MIDI Demo - Budo
 *
 * Demonstrates the MIDI API: device enumeration, input monitoring,
 * output sending, and SysEx message support.
 *
 * Features:
 * - Lists available MIDI input/output devices
 * - Opens the first available input and shows incoming messages
 * - On-screen piano keyboard that sends MIDI out (if output available)
 * - SysEx identity request example
 * - Touch-compatible for Android
 *
 * Keys:
 *   A-K      — Play notes (C4-C5) on white keys
 *   W,E,T,Y,U — Play notes on black keys
 *   S key    — Send SysEx Identity Request
 *   R key    — Refresh device list
 */

// ── Display density ──────────────────────────────────────────────────
var DENSITY = 1;
try { DENSITY = sys.window.getDisplayDensity(); } catch (e) {}
function dp(v) { return Math.round(v * DENSITY); }

// ── SDL scancodes ────────────────────────────────────────────────────
var SDL_A = 4, SDL_S = 22, SDL_D = 7, SDL_F = 9, SDL_G = 10;
var SDL_H = 11, SDL_J = 13, SDL_K = 14;
var SDL_W = 26, SDL_E = 8, SDL_T = 23, SDL_Y = 28, SDL_U = 24;
var SDL_R = 21;
var SDL_X = 27;

// ── Note mappings ────────────────────────────────────────────────────
var whiteKeyMap = {};
whiteKeyMap[SDL_A] = 60;
whiteKeyMap[SDL_S] = 62;
whiteKeyMap[SDL_D] = 64;
whiteKeyMap[SDL_F] = 65;
whiteKeyMap[SDL_G] = 67;
whiteKeyMap[SDL_H] = 69;
whiteKeyMap[SDL_J] = 71;
whiteKeyMap[SDL_K] = 72;

var blackKeyMap = {};
blackKeyMap[SDL_W] = 61;
blackKeyMap[SDL_E] = 63;
blackKeyMap[SDL_T] = 66;
blackKeyMap[SDL_Y] = 68;
blackKeyMap[SDL_U] = 70;

var allKeyMap = {};
for (var k in whiteKeyMap) allKeyMap[k] = whiteKeyMap[k];
for (var k in blackKeyMap) allKeyMap[k] = blackKeyMap[k];

var noteNames = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

function noteName(n) {
    return noteNames[n % 12] + Math.floor(n / 12 - 1);
}

function msgTypeName(type) {
    if (type === sys.midi.NOTE_ON) return 'Note On';
    if (type === sys.midi.NOTE_OFF) return 'Note Off';
    if (type === sys.midi.CONTROL_CHANGE) return 'CC';
    if (type === sys.midi.PROGRAM_CHANGE) return 'Prog';
    if (type === sys.midi.PITCH_BEND) return 'Pitch';
    if (type === sys.midi.CHANNEL_PRESSURE) return 'ChanPr';
    if (type === sys.midi.POLY_PRESSURE) return 'PolyPr';
    if (type === sys.midi.SYSEX) return 'SysEx';
    return '0x' + type.toString(16);
}

// ── State ────────────────────────────────────────────────────────────
var inputHandle = -1;
var outputHandle = -1;
var inputDevices = [];
var outputDevices = [];
var midiLog = [];
var MAX_LOG = 20;
var activeNotes = {};
var lastSysEx = null;

// ── Colors ───────────────────────────────────────────────────────────
var BG = '#1C1C2E';
var PANEL_BG = '#252540';
var ACCENT = '#5B8DEE';
var ACCENT2 = '#E94560';
var TEXT = '#E8E8F0';
var DIM = '#6C6C8A';
var GREEN = '#44CC88';
var WHITE_KEY_COLOR = '#F0F0F5';
var WHITE_KEY_ACTIVE = '#B0D0FF';
var BLACK_KEY_COLOR = '#2A2A3E';
var BLACK_KEY_ACTIVE = '#6688CC';

// ── Initialize ───────────────────────────────────────────────────────
function refreshDevices() {
    sys.midi.refreshDevices();
    inputDevices = sys.midi.getInputDevices();
    outputDevices = sys.midi.getOutputDevices();

    // Close existing handles
    if (inputHandle >= 0) {
        sys.midi.closeInput(inputHandle);
        inputHandle = -1;
    }
    if (outputHandle >= 0) {
        sys.midi.closeOutput(outputHandle);
        outputHandle = -1;
    }

    // Open first available input
    if (inputDevices.length > 0) {
        inputHandle = sys.midi.openInput(0, function(msg) {
            var entry = {};
            if (msg.status === sys.midi.SYSEX && msg.data) {
                entry.text = 'SysEx [' + msg.data.length + ' bytes]';
                entry.detail = '';
                for (var i = 0; i < Math.min(msg.data.length, 16); i++) {
                    entry.detail += ('0' + msg.data[i].toString(16)).slice(-2) + ' ';
                }
                if (msg.data.length > 16) entry.detail += '...';
                entry.color = ACCENT2;
                lastSysEx = msg.data;
            } else {
                var type = msgTypeName(msg.type);
                var ch = msg.channel >= 0 ? msg.channel + 1 : '-';
                entry.text = type + '  ch:' + ch + '  d1:' + msg.data1 + '  d2:' + msg.data2;
                entry.detail = '';
                if (msg.type === sys.midi.NOTE_ON || msg.type === sys.midi.NOTE_OFF) {
                    entry.detail = noteName(msg.data1);
                }
                entry.color = ACCENT;
            }
            entry.time = Date.now();
            midiLog.unshift(entry);
            if (midiLog.length > MAX_LOG) midiLog.pop();
        });
    }

    // Open first available output
    if (outputDevices.length > 0) {
        outputHandle = sys.midi.openOutput(0);
    }
}

refreshDevices();

// ── SysEx Identity Request ───────────────────────────────────────────
function sendIdentityRequest() {
    if (outputHandle < 0) return;
    // Universal SysEx Identity Request: F0 7E 7F 06 01 F7
    sys.midi.sendRaw(outputHandle, [0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7]);
    var entry = {
        text: 'Sent SysEx Identity Request',
        detail: 'F0 7E 7F 06 01 F7',
        color: GREEN,
        time: Date.now()
    };
    midiLog.unshift(entry);
    if (midiLog.length > MAX_LOG) midiLog.pop();
}

// ── Note send helpers ────────────────────────────────────────────────
function playNote(note) {
    if (activeNotes[note]) return;
    activeNotes[note] = true;
    if (outputHandle >= 0) {
        sys.midi.noteOn(outputHandle, 0, note, 100);
    }
}

function stopNote(note) {
    if (!activeNotes[note]) return;
    activeNotes[note] = false;
    if (outputHandle >= 0) {
        sys.midi.noteOff(outputHandle, 0, note, 0);
    }
}

// ── Drawing helpers ──────────────────────────────────────────────────
function drawRoundedRect(x, y, w, h, r) {
    sys.canvas.drawRoundRect(x, y, w, h, r, r);
}

function drawPanel(x, y, w, h, title) {
    sys.canvas.setFillColor(PANEL_BG);
    drawRoundedRect(x, y, w, h, dp(8));
    if (title) {
        sys.canvas.setFillColor(ACCENT);
        sys.canvas.drawText(title, x + dp(12), y + dp(20), dp(14));
    }
}

// ── Piano keyboard ──────────────────────────────────────────────────
var whiteNotes = [60, 62, 64, 65, 67, 69, 71, 72];
var blackNotes = [
    { note: 61, pos: 0 },
    { note: 63, pos: 1 },
    null,
    { note: 66, pos: 3 },
    { note: 68, pos: 4 },
    { note: 70, pos: 5 },
    null
];

function drawPiano(x, y, w, h, pointer) {
    var whiteW = Math.floor(w / 8);
    var blackW = Math.floor(whiteW * 0.6);
    var blackH = Math.floor(h * 0.6);
    var touched = -1;

    // White keys
    for (var i = 0; i < 8; i++) {
        var kx = x + i * whiteW;
        var note = whiteNotes[i];
        var isActive = !!activeNotes[note];

        if (pointer && pointer.down &&
            pointer.x >= kx && pointer.x < kx + whiteW - dp(2) &&
            pointer.y >= y && pointer.y < y + h) {
            touched = note;
        }

        sys.canvas.setFillColor(isActive ? WHITE_KEY_ACTIVE : WHITE_KEY_COLOR);
        drawRoundedRect(kx, y, whiteW - dp(2), h, dp(4));

        sys.canvas.setFillColor(isActive ? ACCENT : DIM);
        sys.canvas.drawText(noteName(note), kx + dp(4), y + h - dp(8), dp(11));
    }

    // Black keys
    for (var i = 0; i < blackNotes.length; i++) {
        if (!blackNotes[i]) continue;
        var note = blackNotes[i].note;
        var pos = blackNotes[i].pos;
        var kx = x + (pos + 1) * whiteW - Math.floor(blackW / 2);
        var isActive = !!activeNotes[note];

        if (pointer && pointer.down &&
            pointer.x >= kx && pointer.x < kx + blackW &&
            pointer.y >= y && pointer.y < y + blackH) {
            touched = note;
        }

        sys.canvas.setFillColor(isActive ? BLACK_KEY_ACTIVE : BLACK_KEY_COLOR);
        drawRoundedRect(kx, y, blackW, blackH, dp(4));
    }

    return touched;
}

// ── Pointer helper ───────────────────────────────────────────────────
var prevTouched = -1;

function getPointer(input) {
    if (input.pointer && input.pointer.down) return input.pointer;
    if (input.mouse) return { x: input.mouse.x, y: input.mouse.y, down: input.mouse.left };
    return { x: 0, y: 0, down: false };
}

// ── Frame ────────────────────────────────────────────────────────────
function frame(timestamp) {
    var W = sys.window.getWidth();
    var H = sys.window.getHeight();
    var input = sys.input.get();
    var pointer = getPointer(input);

    // Handle keyboard input
    if (input.keyboard) {
        for (var key in allKeyMap) {
            var note = allKeyMap[key];
            if (input.keyboard.pressed && input.keyboard.pressed[key]) {
                playNote(note);
            }
            if (input.keyboard.released && input.keyboard.released[key]) {
                stopNote(note);
            }
        }

        // R = Refresh devices
        if (input.keyboard.pressed && input.keyboard.pressed[SDL_R]) {
            refreshDevices();
        }

        // X = Send SysEx identity request
        if (input.keyboard.pressed && input.keyboard.pressed[SDL_X]) {
            sendIdentityRequest();
        }
    }

    // Background
    sys.canvas.clear(BG);

    var margin = dp(16);
    var colW = Math.floor((W - margin * 3) / 2);
    var topY = margin;

    // ── Left panel: Devices ──────────────────────────────────────────
    var devPanelH = dp(180);
    drawPanel(margin, topY, colW, devPanelH, 'MIDI Devices');

    var ty = topY + dp(38);
    sys.canvas.setFillColor(GREEN);
    sys.canvas.drawText('Inputs (' + inputDevices.length + '):', margin + dp(12), ty, dp(12));
    ty += dp(18);

    for (var i = 0; i < inputDevices.length && i < 3; i++) {
        sys.canvas.setFillColor(i === 0 && inputHandle >= 0 ? ACCENT : DIM);
        var label = (i === 0 && inputHandle >= 0 ? '> ' : '  ') + inputDevices[i].name;
        sys.canvas.drawText(label, margin + dp(16), ty, dp(11));
        ty += dp(16);
    }
    if (inputDevices.length === 0) {
        sys.canvas.setFillColor(DIM);
        sys.canvas.drawText('  (none found)', margin + dp(16), ty, dp(11));
        ty += dp(16);
    }

    ty += dp(6);
    sys.canvas.setFillColor(GREEN);
    sys.canvas.drawText('Outputs (' + outputDevices.length + '):', margin + dp(12), ty, dp(12));
    ty += dp(18);

    for (var i = 0; i < outputDevices.length && i < 3; i++) {
        sys.canvas.setFillColor(i === 0 && outputHandle >= 0 ? ACCENT : DIM);
        var label = (i === 0 && outputHandle >= 0 ? '> ' : '  ') + outputDevices[i].name;
        sys.canvas.drawText(label, margin + dp(16), ty, dp(11));
        ty += dp(16);
    }
    if (outputDevices.length === 0) {
        sys.canvas.setFillColor(DIM);
        sys.canvas.drawText('  (none found)', margin + dp(16), ty, dp(11));
    }

    // ── Right panel: MIDI Log ────────────────────────────────────────
    var logPanelH = devPanelH;
    drawPanel(margin * 2 + colW, topY, colW, logPanelH, 'MIDI Input Log');

    var logY = topY + dp(38);
    var logX = margin * 2 + colW + dp(12);
    for (var i = 0; i < midiLog.length && i < 8; i++) {
        var entry = midiLog[i];
        var age = Date.now() - entry.time;
        var alpha = age < 500 ? 255 : Math.max(100, 255 - Math.floor((age - 500) / 20));
        sys.canvas.setFillColor(entry.color);
        sys.canvas.setAlpha(alpha);
        sys.canvas.drawText(entry.text, logX, logY, dp(11));
        if (entry.detail) {
            sys.canvas.setFillColor(DIM);
            sys.canvas.setAlpha(alpha);
            sys.canvas.drawText(entry.detail, logX + dp(4), logY + dp(14), dp(10));
            logY += dp(14);
        }
        logY += dp(16);
    }
    sys.canvas.setAlpha(255);

    // ── Piano keyboard ───────────────────────────────────────────────
    var pianoY = topY + devPanelH + margin;
    var pianoH = dp(120);
    drawPanel(margin, pianoY, W - margin * 2, pianoH + dp(36), 'Keyboard' + (outputHandle >= 0 ? ' (sending to: ' + outputDevices[0].name + ')' : ' (no output)'));

    var touched = drawPiano(margin + dp(8), pianoY + dp(30), W - margin * 2 - dp(16), pianoH, pointer);

    // Handle touch/click piano
    if (pointer.down && touched >= 0) {
        if (prevTouched >= 0 && prevTouched !== touched) {
            stopNote(prevTouched);
        }
        playNote(touched);
        prevTouched = touched;
    } else {
        if (prevTouched >= 0) {
            stopNote(prevTouched);
            prevTouched = -1;
        }
    }

    // ── Bottom: Controls & info ──────────────────────────────────────
    var infoY = pianoY + pianoH + dp(36) + margin;
    drawPanel(margin, infoY, W - margin * 2, dp(60), null);

    sys.canvas.setFillColor(DIM);
    sys.canvas.drawText('A-K: white keys | W,E,T,Y,U: black keys | X: SysEx Identity Request | R: Refresh devices', margin + dp(12), infoY + dp(22), dp(11));
    sys.canvas.drawText('Touch/click piano keys to play | SysEx send/receive supported', margin + dp(12), infoY + dp(42), dp(11));

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
