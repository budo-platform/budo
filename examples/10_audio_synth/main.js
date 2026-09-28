/**
 * Audio Synthesizer Demo
 * 
 * A simple keyboard synthesizer demonstrating the audio API.
 * Touch-compatible for Android devices.
 * 
 * Keys (keyboard):
 * - A, S, D, F, G, H, J, K - White keys (C4 to C5)
 * - W, E, T, Y, U - Black keys
 * - 1-5 - Change waveform
 * - Up/Down - Adjust master volume
 *
 * Touch:
 * - Tap piano keys to play notes
 * - Tap waveform buttons to change sound
 * - Drag volume slider to adjust volume
 */

// SDL Scancodes for keyboard
const SDL_A = 4, SDL_S = 22, SDL_D = 7, SDL_F = 9, SDL_G = 10, SDL_H = 11, SDL_J = 13, SDL_K = 14;
const SDL_W = 26, SDL_E = 8, SDL_T = 23, SDL_Y = 28, SDL_U = 24;
const SDL_1 = 30, SDL_2 = 31, SDL_3 = 32, SDL_4 = 33, SDL_5 = 34;
const SDL_UP = 82, SDL_DOWN = 81;

// Key mapping to MIDI notes (C4 = 60) using SDL scancodes
const keyNotes = {
    [SDL_A]: 60,  // A -> C4
    [SDL_W]: 61,  // W -> C#4
    [SDL_S]: 62,  // S -> D4
    [SDL_E]: 63,  // E -> D#4
    [SDL_D]: 64,  // D -> E4
    [SDL_F]: 65,  // F -> F4
    [SDL_T]: 66,  // T -> F#4
    [SDL_G]: 67,  // G -> G4
    [SDL_Y]: 68,  // Y -> G#4
    [SDL_H]: 69,  // H -> A4
    [SDL_U]: 70,  // U -> A#4
    [SDL_J]: 71,  // J -> B4
    [SDL_K]: 72,  // K -> C5
};

// White key notes (C4-C5)
const whiteKeyNotes = [60, 62, 64, 65, 67, 69, 71, 72];
// Black key notes with their position offset
const blackKeyData = [
    { note: 61, offset: 0 },  // C#4
    { note: 63, offset: 1 },  // D#4
    null,                      // no black key after E
    { note: 66, offset: 3 },  // F#4
    { note: 68, offset: 4 },  // G#4
    { note: 70, offset: 5 },  // A#4
    null                       // no black key after B
];

// Visual key positions with SDL scancodes (for keyboard display)
const whiteKeys = [
    { label: 'A', scancode: SDL_A },
    { label: 'S', scancode: SDL_S },
    { label: 'D', scancode: SDL_D },
    { label: 'F', scancode: SDL_F },
    { label: 'G', scancode: SDL_G },
    { label: 'H', scancode: SDL_H },
    { label: 'J', scancode: SDL_J },
    { label: 'K', scancode: SDL_K }
];
const blackKeys = [
    { label: 'W', offset: 0, scancode: SDL_W },
    { label: 'E', offset: 1, scancode: SDL_E },
    null,
    { label: 'T', offset: 3, scancode: SDL_T },
    { label: 'Y', offset: 4, scancode: SDL_Y },
    { label: 'U', offset: 5, scancode: SDL_U },
    null
];

// Track which source currently owns each note so multiple touches can hold chords safely.
const noteOwners = new Map();
const pointerNotes = new Map();

// Create oscillator pool for polyphony
const numVoices = 8;
const voices = [];
const activeNotes = new Map();
let currentWaveType = sys.audio.SINE;
let waveNames = ['SINE', 'SQUARE', 'SAWTOOTH', 'TRIANGLE', 'NOISE'];

// Start the audio system
sys.audio.start();

// Initialize voices with ultra-fast attack for responsive touch
for (let i = 0; i < numVoices; i++) {
    const osc = sys.audio.createOscillator();
    sys.audio.setOscillatorType(osc, currentWaveType);
    sys.audio.setOscillatorGain(osc, 0.25);
    // Ultra-fast attack (1ms) for instant response, short release for clean cutoff
    sys.audio.setOscillatorEnvelope(osc, 0.001, 0.05, 0.8, 0.1);
    voices.push({ id: osc, note: -1 });
}

// Find free voice or steal oldest
function allocateVoice(note) {
    // First try to find a free voice
    for (let v of voices) {
        if (v.note === -1) {
            v.note = note;
            return v;
        }
    }
    // Steal first voice
    const v = voices[0];
    sys.audio.noteOff(v.id);
    v.note = note;
    return v;
}

function releaseVoice(note) {
    for (let v of voices) {
        if (v.note === note) {
            sys.audio.noteOff(v.id);
            v.note = -1;
            return;
        }
    }
}

function playNote(note) {
    if (activeNotes.has(note)) return;

    const voice = allocateVoice(note);
    const freq = sys.audio.midiToFreq(note);
    sys.audio.setOscillatorFrequency(voice.id, freq);
    sys.audio.noteOn(voice.id);
    activeNotes.set(note, voice);
}

function stopNote(note) {
    if (!activeNotes.has(note)) return;
    releaseVoice(note);
    activeNotes.delete(note);
}

function addNoteOwner(note, ownerId) {
    let owners = noteOwners.get(note);
    if (!owners) {
        owners = new Set();
        noteOwners.set(note, owners);
    }

    if (owners.has(ownerId)) return;

    owners.add(ownerId);
    if (owners.size === 1) {
        playNote(note);
    }
}

function removeNoteOwner(note, ownerId) {
    const owners = noteOwners.get(note);
    if (!owners || !owners.has(ownerId)) return;

    owners.delete(ownerId);
    if (owners.size === 0) {
        noteOwners.delete(note);
        stopNote(note);
    }
}

function setPointerNote(pointerId, note) {
    const ownerId = `pointer:${pointerId}`;
    const previousNote = pointerNotes.has(pointerId) ? pointerNotes.get(pointerId) : -1;

    if (previousNote === note) return;

    if (previousNote >= 0) {
        removeNoteOwner(previousNote, ownerId);
        pointerNotes.delete(pointerId);
    }

    if (note >= 0) {
        addNoteOwner(note, ownerId);
        pointerNotes.set(pointerId, note);
    }
}

function releaseMissingPointers(activePointerIds) {
    for (const pointerId of Array.from(pointerNotes.keys())) {
        if (!activePointerIds.has(pointerId)) {
            setPointerNote(pointerId, -1);
        }
    }
}

function isNoteHeld(note) {
    return noteOwners.has(note);
}

function setWaveType(type) {
    currentWaveType = type;
    for (let v of voices) {
        sys.audio.setOscillatorType(v.id, type);
    }
}

// Key state tracking
const keyDown = new Set();

function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
}

// Helper to check if point is inside rectangle
function hitTest(px, py, rx, ry, rw, rh) {
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

function getLayout(width, height) {
    const whiteKeyAspect = 3.0;
    const blackKeyWidthRatio = 40 / 60;
    const blackKeyHeightRatio = 110 / 180;
    const horizontalPadding = clamp(width * 0.035, 12, 32);
    const topPadding = clamp(height * 0.04, 14, 28);
    const headerTitleSize = clamp(width * 0.04, 20, 30);
    const headerTextSize = clamp(width * 0.018, 11, 16);
    const headerHeight = topPadding + headerTitleSize + headerTextSize * 2 + 26;

    const footerPadding = clamp(height * 0.025, 10, 18);
    const buttonGap = clamp(width * 0.012, 6, 14);
    const buttonHeight = clamp(height * 0.055, 28, 42);
    const sliderHeight = clamp(height * 0.03, 14, 22);
    const footerTextSize = clamp(width * 0.02, 12, 18);
    const footerHeight = footerPadding + buttonHeight + 18 + sliderHeight + footerTextSize + 24;

    const pianoWidth = Math.max(whiteKeys.length, 1);
    const availablePianoWidth = Math.max(160, width - horizontalPadding * 2);
    const availablePianoHeight = Math.max(120, height - headerHeight - footerHeight);
    const keyWidth = Math.min(availablePianoWidth / pianoWidth, availablePianoHeight / whiteKeyAspect);
    const keyHeight = keyWidth * whiteKeyAspect;
    const blackKeyWidth = keyWidth * blackKeyWidthRatio;
    const blackKeyHeight = keyHeight * blackKeyHeightRatio;
    const startX = (width - keyWidth * pianoWidth) / 2;
    const startY = headerHeight + (availablePianoHeight - keyHeight) / 2;

    const buttonRowY = startY + keyHeight + footerPadding;
    const buttonWidth = (width - horizontalPadding * 2 - buttonGap * 4) / 5;
    const sliderLabelWidth = clamp(width * 0.12, 54, 84);
    const sliderValueWidth = clamp(width * 0.11, 44, 70);
    const sliderY = buttonRowY + buttonHeight + 22;
    const sliderX = horizontalPadding + sliderLabelWidth;
    const sliderWidth = width - horizontalPadding * 2 - sliderLabelWidth - sliderValueWidth;

    return {
        horizontalPadding,
        topPadding,
        headerTitleSize,
        headerTextSize,
        footerTextSize,
        keyWidth,
        keyHeight,
        blackKeyWidth,
        blackKeyHeight,
        startX,
        startY,
        buttonGap,
        buttonWidth,
        buttonHeight,
        buttonRowY,
        sliderLabelWidth,
        sliderValueWidth,
        sliderX,
        sliderY,
        sliderWidth,
        sliderHeight
    };
}

// Get touched note from position (returns -1 if none)
function getTouchedNote(mx, my, startX, startY, keyWidth, keyHeight, blackKeyWidth, blackKeyHeight) {
    // Check black keys first (they're on top)
    for (let i = 0; i < blackKeyData.length; i++) {
        if (blackKeyData[i] === null) continue;
        const { note, offset } = blackKeyData[i];
        const x = startX + (offset + 0.7) * keyWidth;
        if (hitTest(mx, my, x, startY, blackKeyWidth, blackKeyHeight)) {
            return note;
        }
    }

    // Check white keys
    for (let i = 0; i < whiteKeyNotes.length; i++) {
        const x = startX + i * keyWidth + 2;
        if (hitTest(mx, my, x, startY, keyWidth - 4, keyHeight)) {
            return whiteKeyNotes[i];
        }
    }

    return -1;
}

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();
    const pointers = Array.isArray(input.pointers) ? input.pointers : [];
    const layout = getLayout(width, height);
    const {
        horizontalPadding,
        topPadding,
        headerTitleSize,
        headerTextSize,
        footerTextSize,
        keyWidth,
        keyHeight,
        blackKeyWidth,
        blackKeyHeight,
        startX,
        startY,
        buttonGap,
        buttonWidth,
        buttonHeight,
        buttonRowY,
        sliderX,
        sliderY,
        sliderWidth,
        sliderHeight
    } = layout;

    // -- Unified pointer handling for mouse drag and multi-touch chords --
    const activePointerIds = new Set();
    for (const pointer of pointers) {
        activePointerIds.add(pointer.id);

        const touchedNote = getTouchedNote(pointer.x, pointer.y, startX, startY, keyWidth, keyHeight, blackKeyWidth, blackKeyHeight);
        setPointerNote(pointer.id, touchedNote);

        if (pointer.pressed) {
            for (let i = 0; i < 5; i++) {
                const btnX = horizontalPadding + i * (buttonWidth + buttonGap);
                if (hitTest(pointer.x, pointer.y, btnX, buttonRowY, buttonWidth, buttonHeight)) {
                    setWaveType(i);
                }
            }
        }

        if (hitTest(pointer.x, pointer.y, sliderX, sliderY, sliderWidth, sliderHeight)) {
            const gain = Math.max(0.0, Math.min(1.0, (pointer.x - sliderX) / sliderWidth));
            sys.audio.setMasterGain(gain);
        }
    }

    releaseMissingPointers(activePointerIds);

    // -- Handle keyboard input for notes --
    for (const [scancode, note] of Object.entries(keyNotes)) {
        const code = parseInt(scancode);
        const ownerId = `key:${code}`;
        if (sys.input.isKeyDown(code)) {
            if (!keyDown.has(code)) {
                keyDown.add(code);
                addNoteOwner(note, ownerId);
            }
        } else {
            if (keyDown.has(code)) {
                keyDown.delete(code);
                removeNoteOwner(note, ownerId);
            }
        }
    }

    // Handle wave type changes (1-5 keys)
    for (let i = 0; i < 5; i++) {
        const scancode = 30 + i; // Keys 1-5 are scancodes 30-34
        if (sys.input.isKeyPressed(scancode)) {
            setWaveType(i);
        }
    }

    // Volume control
    if (sys.input.isKeyDown(82)) { // Up
        const gain = Math.min(1.0, sys.audio.getMasterGain() + 0.01);
        sys.audio.setMasterGain(gain);
    }
    if (sys.input.isKeyDown(81)) { // Down
        const gain = Math.max(0.0, sys.audio.getMasterGain() - 0.01);
        sys.audio.setMasterGain(gain);
    }

    // Draw
    sys.canvas.clear('#1a1a2e');

    // Title
    sys.canvas.setFillColor('#ffffff');
    sys.canvas.drawText('Audio Synthesizer', horizontalPadding, topPadding + headerTitleSize, headerTitleSize);
    sys.canvas.setFillColor('#888888');
    sys.canvas.drawText('Tap keys or use keyboard (A-K, W/E/T/Y/U)', horizontalPadding, topPadding + headerTitleSize + headerTextSize + 8, headerTextSize);
    sys.canvas.drawText('Wave 1-5 | Up/Down volume | Drag slider', horizontalPadding, topPadding + headerTitleSize + headerTextSize * 2 + 14, headerTextSize);

    // White keys
    const whiteRadius = clamp(keyWidth * 0.08, 4, 10);
    const whiteLabelSize = clamp(keyWidth * 0.32, 14, 28);
    for (let i = 0; i < whiteKeys.length; i++) {
        const x = startX + i * keyWidth;
        const { label, scancode } = whiteKeys[i];
        const note = whiteKeyNotes[i];
        const isPressed = keyDown.has(scancode) || isNoteHeld(note);

        sys.canvas.setFillColor(isPressed ? '#4CAF50' : '#ffffff');
        sys.canvas.drawRoundRect(x + 2, startY, keyWidth - 4, keyHeight, whiteRadius, whiteRadius);

        sys.canvas.setFillColor(isPressed ? '#ffffff' : '#333333');
        sys.canvas.drawText(label, x + keyWidth * 0.5 - whiteLabelSize * 0.3, startY + keyHeight - whiteLabelSize * 0.5, whiteLabelSize);
    }

    // Black keys
    const blackRadius = clamp(blackKeyWidth * 0.08, 4, 8);
    const blackLabelSize = clamp(blackKeyWidth * 0.32, 11, 18);
    for (let i = 0; i < blackKeys.length; i++) {
        if (blackKeys[i] === null) continue;

        const { label, offset, scancode } = blackKeys[i];
        const note = blackKeyData[i].note;
        const x = startX + (offset + 0.7) * keyWidth;
        const isPressed = keyDown.has(scancode) || isNoteHeld(note);

        sys.canvas.setFillColor(isPressed ? '#4CAF50' : '#1a1a1a');
        sys.canvas.drawRoundRect(x, startY, blackKeyWidth, blackKeyHeight, blackRadius, blackRadius);

        sys.canvas.setFillColor('#ffffff');
        sys.canvas.drawText(label, x + blackKeyWidth * 0.5 - blackLabelSize * 0.3, startY + blackKeyHeight - blackLabelSize * 0.45, blackLabelSize);
    }

    // Wave type indicator
    sys.canvas.setFillColor('#ffffff');
    sys.canvas.drawText('Waveform', horizontalPadding, buttonRowY - 10, footerTextSize);

    const buttonTextSize = clamp(buttonWidth * 0.15, 9, 14);
    const buttonRadius = clamp(buttonHeight * 0.2, 5, 8);

    for (let i = 0; i < 5; i++) {
        const x = horizontalPadding + i * (buttonWidth + buttonGap);
        const isSelected = currentWaveType === i;

        sys.canvas.setFillColor(isSelected ? '#4CAF50' : '#333333');
        sys.canvas.drawRoundRect(x, buttonRowY, buttonWidth, buttonHeight, buttonRadius, buttonRadius);

        sys.canvas.setFillColor('#ffffff');
        sys.canvas.drawText(`${i + 1}:${waveNames[i]}`, x + buttonWidth * 0.08, buttonRowY + buttonHeight * 0.68, buttonTextSize);
    }

    // Volume indicator
    const volume = sys.audio.getMasterGain();

    sys.canvas.setFillColor('#ffffff');
    sys.canvas.drawText('Volume', horizontalPadding, sliderY + sliderHeight * 0.8, footerTextSize);

    sys.canvas.setFillColor('#333333');
    sys.canvas.drawRoundRect(sliderX, sliderY, sliderWidth, sliderHeight, 4, 4);

    sys.canvas.setFillColor('#4CAF50');
    sys.canvas.drawRoundRect(sliderX, sliderY, sliderWidth * volume, sliderHeight, 4, 4);

    sys.canvas.setFillColor('#ffffff');
    sys.canvas.drawText(`${Math.round(volume * 100)}%`, sliderX + sliderWidth + 10, sliderY + sliderHeight * 0.8, footerTextSize);

    // Active voices indicator
    sys.canvas.setFillColor('#888888');
    sys.canvas.drawText(`Active voices: ${activeNotes.size}/${numVoices}`, horizontalPadding, height - 12, clamp(width * 0.016, 11, 14));

    sys.animation.requestFrame(frame);
}

sys.log('Audio Synthesizer Demo');
sys.log('Touch keys to play or use keyboard (A-K, W/E/T/Y/U)');
sys.animation.requestFrame(frame);
