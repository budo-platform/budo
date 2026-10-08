/// <reference path="budo.d.ts" />

import { createUI } from './ui/budo-ui.js';

interface MidiCommand {
    key: string;
    label: string;
    bytes: number[];
    receivedAt: number;
}

interface NoteSequenceEvent {
    timeMs: number;
    label: string;
    bytes: number[];
}

interface CaptureState {
    byKey: Record<string, MidiCommand>;
    sysex: MidiCommand[];
}

interface LogEntry {
    text: string;
    detail: string;
    color: string;
    time: number;
}

interface NameEditor {
    active: boolean;
    title: string;
    value: string;
    caret: number;
    compositionText: string;
    compositionCaret: number;
    blinkStarted: number;
    action: string;
    targetId: number;
    needsFocus: boolean;
}

interface PresetMenu {
    active: boolean;
    presetId: number;
}

interface SongMenu {
    active: boolean;
    songId: number;
}

interface ImportDialog {
    active: boolean;
    files: FileEntry[];
    selected: number;
    error: string;
}

const database = sys.db.open('midi_preset_saver');

const glassProgram = sys.gl.createProgram('glass.vert', 'glass.frag');

const UI_FONT_NAME = 'Barlow Semi Condensed Medium';

function loadUiFont(): void {
    sys.font.load('BarlowSemiCondensed-Medium.ttf', UI_FONT_NAME);
    sys.canvas.setFont(UI_FONT_NAME);
    sys.log('Custom UI font loaded');
}

sys.db.execute(database, `
    CREATE TABLE IF NOT EXISTS songs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL,
        sort_order INTEGER DEFAULT 0,
        created_at TEXT DEFAULT (datetime('now'))
    )
`);

sys.db.execute(database, `
    CREATE TABLE IF NOT EXISTS presets (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        song_id INTEGER NOT NULL,
        name TEXT NOT NULL,
        payload TEXT NOT NULL,
        command_count INTEGER DEFAULT 0,
        created_at TEXT DEFAULT (datetime('now')),
        updated_at TEXT DEFAULT (datetime('now'))
    )
`);

sys.db.execute(database, `
    CREATE TABLE IF NOT EXISTS preset_sequences (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        preset_id INTEGER NOT NULL,
        payload TEXT NOT NULL,
        event_count INTEGER DEFAULT 0,
        duration_ms INTEGER DEFAULT 0,
        created_at TEXT DEFAULT (datetime('now')),
        updated_at TEXT DEFAULT (datetime('now'))
    )
`);

function ensurePresetSequencesAllowMultiple(): void {
    const indexes = sys.db.query(database, 'PRAGMA index_list(preset_sequences)');
    let hasUniquePresetSequenceIndex = false;
    for (let index = 0; index < indexes.length; index++) {
        const row = indexes[index];
        if (Number(row.unique || row['unique'] || 0) !== 0) {
            hasUniquePresetSequenceIndex = true;
        }
    }

    if (hasUniquePresetSequenceIndex) {
        sys.db.execute(database, 'DROP TABLE IF EXISTS preset_sequences_migration');
        sys.db.execute(database, `
            CREATE TABLE preset_sequences_migration (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                preset_id INTEGER NOT NULL,
                payload TEXT NOT NULL,
                event_count INTEGER DEFAULT 0,
                duration_ms INTEGER DEFAULT 0,
                created_at TEXT DEFAULT (datetime('now')),
                updated_at TEXT DEFAULT (datetime('now'))
            )
        `);
        sys.db.execute(database, `
            INSERT INTO preset_sequences_migration (id, preset_id, payload, event_count, duration_ms, created_at, updated_at)
            SELECT id, preset_id, payload, event_count, duration_ms, created_at, updated_at FROM preset_sequences ORDER BY id ASC
        `);
        sys.db.execute(database, 'DROP TABLE preset_sequences');
        sys.db.execute(database, 'ALTER TABLE preset_sequences_migration RENAME TO preset_sequences');
    }

    sys.db.execute(database, 'CREATE INDEX IF NOT EXISTS idx_preset_sequences_preset_id ON preset_sequences (preset_id, id)');
}

ensurePresetSequencesAllowMultiple();

function ensurePresetsHaveSortOrder(): void {
    const columns = sys.db.query(database, 'PRAGMA table_info(presets)');
    let hasSortOrder = false;
    for (let index = 0; index < columns.length; index++) {
        if (String(columns[index].name || '') === 'sort_order') hasSortOrder = true;
    }
    if (!hasSortOrder) {
        sys.db.execute(database, 'ALTER TABLE presets ADD COLUMN sort_order INTEGER DEFAULT 0');
        sys.db.execute(database, 'UPDATE presets SET sort_order = (SELECT COUNT(*) FROM presets p2 WHERE p2.song_id = presets.song_id AND p2.id <= presets.id)');
    }
}

ensurePresetsHaveSortOrder();

sys.db.execute(database, `
    CREATE TABLE IF NOT EXISTS app_state (
        key TEXT PRIMARY KEY,
        value TEXT NOT NULL
    )
`);

const COLORS = {
    bg: '#171512',
    panel: '#292622',
    panelSoft: '#35312B',
    panelStrong: '#484139',
    text: '#E9E0C7',
    muted: '#B5AA8F',
    faint: '#776E5D',
    disabled: '#948873',
    border: '#4C453B',
    teal: '#E58A61',
    amber: '#D5A84E',
    blue: '#789C98',
    red: '#E98273',
    green: '#91AD72',
    black: '#100E0C',
    primaryHover: '#F09C75',
    sendHover: '#A4BE84',
    warnHover: '#E5BA62',
    dangerHover: '#F29384',
    blueHover: '#8FB2AE',
    sequencePlaying: '#3D342A',
    livePreset: '#27231F',
    livePresetRecalled: '#3C352B',
    recallBanner: '#2A3227',
    selectedRow: '#40362B',
    draggedRow: '#51483C',
    selectedSong: '#49392D',
    songAccentRose: '#A77B73',
    songAccentCopper: '#B8875C'
};

const SDL_X = 27;
const SDL_ENTER = 40;
const SDL_ESCAPE = 41;
const SDL_BACKSPACE = 42;
const SDL_RIGHT = 79;
const SDL_LEFT = 80;

const MAX_SEQUENCE_EVENTS = 256;
const MIN_PLAYBACK_NOTE_MS = 45;
/** Past the event limit, releases (note-offs, pedals up) are still recorded,
 *  so a memo never ends with a note held. */
const MAX_SEQUENCE_RELEASE_EVENTS = 160;
/** A playback this far behind (a stalled frame) skips ahead instead of
 *  sending every overdue event at once. */
const PLAYBACK_MAX_LATE_MS = 250;
/** Pause after each SysEx during a recall, so the device can apply a dump
 *  before the next message (channel messages go out back to back). */
const RECALL_SYSEX_GAP_MS = 20;
/** Largest export the import accepts. */
const MAX_EXPORT_BYTES = 64 * 1024 * 1024;

/** Pedals: performance gestures, recorded in memos, not preset state. */
function isPedalController(controller: number): boolean {
    if (controller === 64) return true;
    if (controller === 66) return true;
    if (controller === 67) return true;
    return false;
}

/** Controllers that are not preset state: pedals, and the channel-mode
 *  messages 120-127 (all sound off, reset controllers, local control, all
 *  notes off, omni and mono/poly), which must never be replayed by a recall. */
function isTransientController(controller: number): boolean {
    if (isPedalController(controller)) return true;
    if (controller >= 120) return true;
    return false;
}

let songs: any[] = [];
let presets: any[] = [];
let livePresets: any[] = [];
let presetSequences: any[] = [];
let selectedSongId = 0;
let selectedPresetId = 0;
let lastRecalledPresetId = 0;
let lastRecallTime = 0;
let songScroll = 0;
let presetScroll = 0;
let sequenceScroll = 0;
let liveMode = false;
/** Capture order: the last receivedAt handed out. */
let lastCaptureTime = 0;
/** The frame clock (monotonic ms, starting near 0) that times playback and
 *  recall pacing. */
let frameClockMs = 0;
let frameClockSet = false;
/** MIDI devices are remembered by name: list positions shift on hot-plug. */
let selectedInputName = '';
let selectedOutputName = '';
let deviceNamesLoaded = false;
let midiAvailable = true;
try {
    midiAvailable = sys.capabilities.midi.available;
} catch (error) {
    midiAvailable = true;
}

let inputDevices: MidiDevice[] = [];
let outputDevices: MidiDevice[] = [];
let selectedInputIndex = 0;
let selectedOutputIndex = 0;
let inputHandle = -1;
let outputHandle = -1;

const capture: CaptureState = {
    byKey: {},
    sysex: []
};

let sysexSerial = 0;
let sequenceRecording = false;
let sequenceRecordStartedAt = 0;
let sequenceRecordMidiStartedAtUs = 0;
let sequenceRecordPresetId = 0;
let sequenceRecordingEvents: NoteSequenceEvent[] = [];
/** While recording: notes and pedals held down, by "channel:number", so Stop
 *  and the event limit can release them. */
let sequenceRecordHeld: Record<string, boolean> = {};
let sequenceRecordPedals: Record<string, boolean> = {};
let sequenceRecordClockSet = false;
let sequenceRecordLastNativeUs = 0;
let sequencePlaying = false;
let sequencePlayingId = 0;
let sequencePlaybackStartedAt = 0;
let sequencePlaybackEvents: NoteSequenceEvent[] = [];
let sequencePlaybackIndex = 0;
let sequencePlaybackSent = 0;
let sequencePlaybackName = '';
/** Notes and pedals the playback has sent down and not released yet. */
let sequencePlaybackHeld: Record<string, boolean> = {};
let sequencePlaybackPedals: Record<string, boolean> = {};
let eventLog: LogEntry[] = [];

const editor: NameEditor = {
    active: false,
    title: '',
    value: '',
    caret: 0,
    compositionText: '',
    compositionCaret: 0,
    blinkStarted: 0,
    action: '',
    targetId: 0,
    needsFocus: false
};

const presetMenu: PresetMenu = {
    active: false,
    presetId: 0
};

const songMenu: SongMenu = {
    active: false,
    songId: 0
};

const importDialog: ImportDialog = {
    active: false,
    files: [],
    selected: 0,
    error: ''
};

function now(): number {
    return Date.now();
}

function addLog(text: string, detail: string, color: string): void {
    eventLog.unshift({ text: text, detail: detail, color: color, time: now() });
    while (eventLog.length > 54) {
        eventLog.pop();
    }
}

function rowId(row: any): number {
    return Number(row.id || 0);
}

function rowName(row: any): string {
    return String(row.name || 'Untitled');
}

function clamp(value: number, minimum: number, maximum: number): number {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

function wrapIndex(index: number, length: number): number {
    if (length <= 0) return 0;
    let next = index % length;
    if (next < 0) next += length;
    return next;
}

function indexOfRowId(rows: any[], id: number): number {
    for (let index = 0; index < rows.length; index++) {
        if (rowId(rows[index]) === id) return index;
    }
    return -1;
}

function moveInArray(rows: any[], fromIndex: number, toIndex: number): void {
    // NOTE: keep this guard-free. The lightweight TS stripper mis-handles a
    // function whose body mixes `===` and `.length` comparisons and silently
    // drops the splice mutation. Callers must pass valid, distinct, in-range
    // indices (updateReorderDrag clamps target and checks target !== current).
    const item = rows.splice(fromIndex, 1)[0];
    rows.splice(toIndex, 0, item);
}

function persistSongOrder(): void {
    for (let index = 0; index < songs.length; index++) {
        sys.db.run(database, 'UPDATE songs SET sort_order = ? WHERE id = ?', index + 1, rowId(songs[index]));
    }
}

function persistPresetOrder(): void {
    for (let index = 0; index < presets.length; index++) {
        sys.db.run(database, 'UPDATE presets SET sort_order = ? WHERE id = ?', index + 1, rowId(presets[index]));
    }
}

function persistPresetOrderForSong(songId: number): void {
    const rows = editPresetsForSong(songId);
    for (let index = 0; index < rows.length; index++) {
        sys.db.run(database, 'UPDATE presets SET sort_order = ? WHERE id = ? AND song_id = ?',
            index + 1, rowId(rows[index]), songId);
    }
}

function sanitizeName(value: string, fallback: string): string {
    let cleaned = value.trim();
    if (cleaned.length === 0) cleaned = fallback;
    if (cleaned.length > 48) cleaned = cleaned.slice(0, 48);
    return cleaned;
}

function saveAppState(key: string, value: string): void {
    sys.db.run(database, 'DELETE FROM app_state WHERE key = ?', key);
    sys.db.run(database, 'INSERT INTO app_state (key, value) VALUES (?, ?)', key, value);
}

function loadAppState(key: string, fallback: string): string {
    const rows = sys.db.query(database, 'SELECT value FROM app_state WHERE key = ?', key);
    if (rows.length === 0) return fallback;
    return String(rows[0].value || fallback);
}

function hasRowWithId(rows: any[], id: number): boolean {
    for (let index = 0; index < rows.length; index++) {
        if (rowId(rows[index]) === id) return true;
    }
    return false;
}

function ensureDefaultSong(): void {
    const rows = sys.db.query(database, 'SELECT COUNT(*) AS count FROM songs');
    let count = 0;
    if (rows.length > 0) {
        count = Number(rows[0].count || 0);
    }
    if (count === 0) {
        sys.db.run(database, 'INSERT INTO songs (name, sort_order) VALUES (?, ?)', 'Song 1', 1);
    }
}

function loadSongs(): void {
    songs = sys.db.query(database, 'SELECT songs.*, (SELECT COUNT(*) FROM presets WHERE presets.song_id = songs.id) AS preset_count FROM songs ORDER BY sort_order ASC, id ASC');
    if (songs.length === 0) {
        selectedSongId = 0;
        presets = [];
        selectedPresetId = 0;
        loadPresetSequences();
        return;
    }

    if (selectedSongId === 0 || !hasRowWithId(songs, selectedSongId)) {
        selectedSongId = rowId(songs[0]);
    }
    saveAppState('selected_song_id', String(selectedSongId));
    loadPresets();
}

function loadPresets(): void {
    loadLivePresets();
    if (selectedSongId <= 0) {
        presets = [];
        selectedPresetId = 0;
        loadPresetSequences();
        return;
    }
    presets = sys.db.query(database, 'SELECT * FROM presets WHERE song_id = ? ORDER BY sort_order ASC, id ASC', selectedSongId);
    if (presets.length === 0) {
        selectedPresetId = 0;
        loadPresetSequences();
        return;
    }
    if (selectedPresetId === 0 || !hasRowWithId(presets, selectedPresetId)) {
        selectedPresetId = rowId(presets[0]);
    }
    saveAppState('selected_preset_id', String(selectedPresetId));
    loadPresetSequences();
}

function loadLivePresets(): void {
    livePresets = sys.db.query(database, `
        SELECT presets.*, songs.id AS live_song_id, songs.name AS live_song_name,
               songs.sort_order AS live_song_order
        FROM presets
        JOIN songs ON songs.id = presets.song_id
        ORDER BY songs.sort_order ASC, songs.id ASC, presets.sort_order ASC, presets.id ASC
    `);
}

function loadPresetSequences(): void {
    presetSequences = [];
    sequenceScroll = 0;
    if (selectedPresetId <= 0) return;
    presetSequences = sys.db.query(database, 'SELECT * FROM preset_sequences WHERE preset_id = ? ORDER BY id DESC', selectedPresetId);
}

function createSong(name: string): void {
    const rows = sys.db.query(database, 'SELECT COALESCE(MAX(sort_order), 0) + 1 AS next_order FROM songs');
    let sortOrder = 1;
    if (rows.length > 0) {
        sortOrder = Number(rows[0].next_order || 1);
    }
    sys.db.run(database, 'INSERT INTO songs (name, sort_order) VALUES (?, ?)', sanitizeName(name, 'Song'), sortOrder);
    selectedSongId = sys.db.lastInsertId(database);
    selectedPresetId = 0;
    loadSongs();
    addLog('Song saved', '', COLORS.green);
}

function renameSong(id: number, name: string): void {
    if (id <= 0) return;
    sys.db.run(database, 'UPDATE songs SET name = ? WHERE id = ?', sanitizeName(name, 'Song'), id);
    loadSongs();
    addLog('Song renamed', '', COLORS.green);
}

function deleteSong(id: number): void {
    if (id <= 0) {
        addLog('Select a song to delete', '', COLORS.amber);
        return;
    }

    let song = null;
    for (let index = 0; index < songs.length; index++) {
        if (rowId(songs[index]) === id) song = songs[index];
    }
    if (!song) {
        addLog('Select a song to delete', '', COLORS.amber);
        return;
    }

    const name = rowName(song);
    stopSequencePlayback();
    sys.db.run(database, 'DELETE FROM preset_sequences WHERE preset_id IN (SELECT id FROM presets WHERE song_id = ?)', id);
    sys.db.run(database, 'DELETE FROM presets WHERE song_id = ?', id);
    sys.db.run(database, 'DELETE FROM songs WHERE id = ?', id);
    if (selectedSongId === id) selectedSongId = 0;
    selectedPresetId = 0;
    lastRecalledPresetId = 0;
    presetScroll = 0;
    ensureDefaultSong();
    loadSongs();
    addLog('Deleted song', name, COLORS.amber);
}

function selectedSongName(): string {
    for (let index = 0; index < songs.length; index++) {
        if (rowId(songs[index]) === selectedSongId) return rowName(songs[index]);
    }
    return 'No song';
}

function selectedPresetName(): string {
    for (let index = 0; index < presets.length; index++) {
        if (rowId(presets[index]) === selectedPresetId) return rowName(presets[index]);
    }
    return 'No preset';
}

function selectedPreset(): any {
    for (let index = 0; index < presets.length; index++) {
        if (rowId(presets[index]) === selectedPresetId) return presets[index];
    }
    return null;
}

function liveConnectionOk(): boolean {
    if (!midiAvailable) return false;
    if (outputHandle < 0) return false;
    if (outputDevices.length === 0) return false;
    return true;
}

function nextSongName(): string {
    return 'Song ' + (songs.length + 1);
}

function nextPresetName(): string {
    return 'Preset ' + (presets.length + 1);
}

function byteValue(value: number): number {
    const numeric = Math.floor(Number(value || 0));
    return clamp(numeric, 0, 255);
}

function copyBytes(source: any): number[] {
    const bytes: number[] = [];
    if (!source) return bytes;
    const length = Number(source.length || 0);
    for (let index = 0; index < length; index++) {
        bytes.push(byteValue(source[index]));
    }
    return bytes;
}

function hexByte(value: number): string {
    return ('0' + byteValue(value).toString(16).toUpperCase()).slice(-2);
}

function bytePreview(bytes: number[], maxBytes: number): string {
    let text = '';
    const count = Math.min(bytes.length, maxBytes);
    for (let index = 0; index < count; index++) {
        if (index > 0) text += ' ';
        text += hexByte(bytes[index]);
    }
    if (bytes.length > maxBytes) text += ' ...';
    return text;
}

function channelLabel(channel: number): string {
    if (channel < 0) return '-';
    return String(channel + 1);
}

function deviceName(devices: MidiDevice[], index: number, fallback: string): string {
    if (devices.length === 0) return fallback;
    const safeIndex = wrapIndex(index, devices.length);
    return devices[safeIndex].name;
}

function messageTypeName(type: number): string {
    if (type === sys.midi.CONTROL_CHANGE) return 'CC';
    if (type === sys.midi.PROGRAM_CHANGE) return 'Program';
    if (type === sys.midi.PITCH_BEND) return 'Pitch Bend';
    if (type === sys.midi.CHANNEL_PRESSURE) return 'Channel Pressure';
    if (type === sys.midi.POLY_PRESSURE) return 'Poly Pressure';
    if (type === sys.midi.SYSEX) return 'SysEx';
    if (type === sys.midi.NOTE_ON) return 'Note On';
    if (type === sys.midi.NOTE_OFF) return 'Note Off';
    return '0x' + byteValue(type).toString(16).toUpperCase();
}

/** Arrival time, made strictly increasing: messages delivered in the same
 *  millisecond (a Bank Select and Program Change burst) keep their order. */
function nextCaptureTime(): number {
    let time = now();
    if (time <= lastCaptureTime) time = lastCaptureTime + 0.001;
    lastCaptureTime = time;
    return time;
}

function playbackNow(): number {
    if (frameClockSet) return frameClockMs;
    return now();
}

function commandFromMidiMessage(message: MidiMessage): MidiCommand | null {
    const receivedAt = nextCaptureTime();
    const type = Number(message.type || 0);
    const channel = Number(message.channel || 0);
    const status = byteValue(message.status);
    const data1 = byteValue(message.data1);
    const data2 = byteValue(message.data2);

    if ((status === sys.midi.SYSEX || type === sys.midi.SYSEX) && message.data) {
        const bytes = copyBytes(message.data);
        if (bytes.length === 0) return null;
        sysexSerial += 1;
        return {
            key: 'sysex:' + sysexSerial,
            label: 'SysEx ' + bytes.length + ' bytes',
            bytes: bytes,
            receivedAt: receivedAt
        };
    }

    if (type === sys.midi.CONTROL_CHANGE) {
        if (isTransientController(data1)) return null;
        return {
            key: 'cc:' + channel + ':' + data1,
            label: 'CC ch ' + channelLabel(channel) + ' #' + data1 + ' = ' + data2,
            bytes: [status, data1, data2],
            receivedAt: receivedAt
        };
    }

    if (type === sys.midi.PROGRAM_CHANGE) {
        return {
            key: 'program:' + channel,
            label: 'Program ch ' + channelLabel(channel) + ' = ' + data1,
            bytes: [status, data1],
            receivedAt: receivedAt
        };
    }

    if (type === sys.midi.PITCH_BEND) {
        const bend = ((data2 & 0x7F) << 7) + (data1 & 0x7F) - 8192;
        return {
            key: 'pitch:' + channel,
            label: 'Pitch ch ' + channelLabel(channel) + ' = ' + bend,
            bytes: [status, data1, data2],
            receivedAt: receivedAt
        };
    }

    // Channel and poly pressure are not state: they change continuously while
    // keys are held (one stored message per key ever touched, for poly).

    return null;
}

function captureCommand(command: MidiCommand): void {
    if (command.key.indexOf('sysex:') === 0) {
        capture.sysex.push(command);
        while (capture.sysex.length > 24) {
            capture.sysex.shift();
        }
    } else {
        capture.byKey[command.key] = command;
    }
}

function capturedCommandsNewestFirst(): MidiCommand[] {
    const commands: MidiCommand[] = [];
    const keys = Object.keys(capture.byKey);
    for (let index = 0; index < keys.length; index++) {
        commands.push(capture.byKey[keys[index]]);
    }
    for (let index = 0; index < capture.sysex.length; index++) {
        commands.push(capture.sysex[index]);
    }
    commands.sort(function (a: MidiCommand, b: MidiCommand): number {
        return b.receivedAt - a.receivedAt;
    });
    return commands;
}

function snapshotCommands(): MidiCommand[] {
    const commands = capturedCommandsNewestFirst();
    commands.sort(function (a: MidiCommand, b: MidiCommand): number {
        return a.receivedAt - b.receivedAt;
    });

    const snapshot: MidiCommand[] = [];
    for (let index = 0; index < commands.length; index++) {
        const command = commands[index];
        snapshot.push({
            key: command.key,
            label: command.label,
            bytes: copyBytes(command.bytes),
            receivedAt: command.receivedAt
        });
    }
    return snapshot;
}

function capturedCount(): number {
    return Object.keys(capture.byKey).length + capture.sysex.length;
}

function clearCapture(): void {
    capture.byKey = {};
    capture.sysex = [];
    addLog('Captured state cleared', '', COLORS.amber);
}

function encodePreset(commands: MidiCommand[]): string {
    return JSON.stringify({ version: 1, commands: commands });
}

function decodePreset(payload: string): MidiCommand[] {
    try {
        const parsed = JSON.parse(payload || '{}');
        let rawCommands = parsed.commands;
        if (Array.isArray(parsed)) {
            rawCommands = parsed;
        }
        if (!Array.isArray(rawCommands)) return [];

        const commands: MidiCommand[] = [];
        for (let index = 0; index < rawCommands.length; index++) {
            const raw = rawCommands[index];
            const bytes = copyBytes(raw.bytes);
            if (bytes.length === 0) continue;
            commands.push({
                key: String(raw.key || 'raw:' + index),
                label: String(raw.label || bytePreview(bytes, 8)),
                bytes: bytes,
                receivedAt: Number(raw.receivedAt || index)
            });
        }
        return commands;
    } catch (error) {
        return [];
    }
}

function noteName(note: number): string {
    const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
    const safeNote = clamp(byteValue(note), 0, 127);
    const octave = Math.floor(safeNote / 12) - 1;
    return names[safeNote % 12] + octave;
}

function midiMessageType(message: MidiMessage): number {
    const type = Number(message.type || 0);
    if (type > 0) return type;
    const status = byteValue(message.status);
    if (status >= 0x80 && status < 0xF0) return status & 0xF0;
    return status;
}

function midiMessageChannel(message: MidiMessage): number {
    let channel = Number(message.channel);
    if (channel !== channel) {
        const status = byteValue(message.status);
        if (status >= 0x80 && status < 0xF0) channel = status & 0x0F;
    }
    if (channel !== channel) channel = 0;
    return clamp(channel, -1, 15);
}

/** Milliseconds since Rec for a message stamped `timestampUs` (0: none). The
 *  device clock is aligned with the wall clock on its first timestamp, and
 *  again if it restarts (a reopened port), so mixed and restarted timestamps
 *  stay on one timeline. */
function recordingTimeMs(timestampUs: number): number {
    const wallMs = Math.max(0, now() - sequenceRecordStartedAt);
    if (timestampUs <= 0) return wallMs;
    if (!sequenceRecordClockSet || timestampUs < sequenceRecordLastNativeUs) {
        sequenceRecordMidiStartedAtUs = timestampUs - wallMs * 1000;
        sequenceRecordClockSet = true;
    }
    sequenceRecordLastNativeUs = timestampUs;
    return Math.max(0, Math.floor((timestampUs - sequenceRecordMidiStartedAtUs) / 1000));
}

function sequenceEventFromMidiMessage(message: MidiMessage): NoteSequenceEvent | null {
    const type = midiMessageType(message);
    const isNote = type === sys.midi.NOTE_ON || type === sys.midi.NOTE_OFF;
    const isPedal = type === sys.midi.CONTROL_CHANGE && isPedalController(byteValue(message.data1));
    if (!isNote && !isPedal) return null;

    const channel = midiMessageChannel(message);
    let status = byteValue(message.status);
    if (status === 0) {
        status = type + clamp(channel, 0, 15);
    }
    const data1 = byteValue(message.data1);
    const data2 = byteValue(message.data2);
    const timeMs = recordingTimeMs(Number(message.timestamp || 0));
    if (isPedal) {
        let state = 'up';
        if (data2 >= 64) state = 'down';
        return {
            timeMs: timeMs,
            label: 'Pedal ch ' + channelLabel(channel) + ' #' + data1 + ' ' + state,
            bytes: [status, data1, data2]
        };
    }
    let label = 'Note On';
    if (type === sys.midi.NOTE_OFF || data2 === 0) {
        label = 'Note Off';
    }
    return {
        timeMs: timeMs,
        label: label + ' ch ' + channelLabel(channel) + ' ' + noteName(data1) + ' v ' + data2,
        bytes: [status, data1, data2]
    };
}

/** True for a note-off or a pedal going up. */
function isReleaseEvent(event: NoteSequenceEvent): boolean {
    if (isSequenceNoteOff(event)) return true;
    if (sequenceEventType(event) === sys.midi.CONTROL_CHANGE && sequenceEventVelocity(event) < 64) return true;
    return false;
}

/** Note-offs and pedal releases at `timeMs` for what `held` and `pedals`
 *  (keys "channel:number") still hold down. */
function releaseEvents(held: Record<string, boolean>, pedals: Record<string, boolean>, timeMs: number): NoteSequenceEvent[] {
    const events: NoteSequenceEvent[] = [];
    const notes = Object.keys(held);
    for (let index = 0; index < notes.length; index++) {
        const parts = notes[index].split(':');
        const channel = Number(parts[0]);
        const note = Number(parts[1]);
        events.push({
            timeMs: timeMs,
            label: 'Note Off ch ' + channelLabel(channel) + ' ' + noteName(note) + ' v 0',
            bytes: [0x80 + channel, note, 0]
        });
    }
    const controllers = Object.keys(pedals);
    for (let index = 0; index < controllers.length; index++) {
        const parts = controllers[index].split(':');
        const channel = Number(parts[0]);
        const controller = Number(parts[1]);
        events.push({
            timeMs: timeMs,
            label: 'Pedal ch ' + channelLabel(channel) + ' #' + controller + ' up',
            bytes: [0xB0 + channel, controller, 0]
        });
    }
    return events;
}

/** Update `held` and `pedals` with an event that was sent or recorded. */
function trackHeld(event: NoteSequenceEvent, held: Record<string, boolean>, pedals: Record<string, boolean>): void {
    const key = noteEventKey(event);
    if (isSequenceNoteOn(event)) {
        held[key] = true;
    } else if (isSequenceNoteOff(event)) {
        delete held[key];
    } else if (sequenceEventType(event) === sys.midi.CONTROL_CHANGE) {
        if (sequenceEventVelocity(event) >= 64) pedals[key] = true;
        else delete pedals[key];
    }
}

function copySequenceEvents(events: NoteSequenceEvent[]): NoteSequenceEvent[] {
    const copied: NoteSequenceEvent[] = [];
    for (let index = 0; index < events.length; index++) {
        const event = events[index];
        const bytes = copyBytes(event.bytes);
        if (bytes.length === 0) continue;
        copied.push({
            timeMs: Math.max(0, Math.floor(Number(event.timeMs || 0))),
            label: String(event.label || bytePreview(bytes, 8)),
            bytes: bytes
        });
    }
    return copied;
}

function normalizedSequenceEvents(events: NoteSequenceEvent[]): NoteSequenceEvent[] {
    const copied = copySequenceEvents(events);
    copied.sort(function (a: NoteSequenceEvent, b: NoteSequenceEvent): number {
        return a.timeMs - b.timeMs;
    });
    if (copied.length === 0) return copied;

    const offset = copied[0].timeMs;
    for (let index = 0; index < copied.length; index++) {
        copied[index].timeMs = Math.max(0, copied[index].timeMs - offset);
    }
    return copied;
}

function encodeNoteSequence(events: NoteSequenceEvent[]): string {
    return JSON.stringify({ version: 1, events: events });
}

function decodeNoteSequence(payload: string): NoteSequenceEvent[] {
    try {
        const parsed = JSON.parse(payload || '{}');
        let rawEvents = parsed.events;
        if (Array.isArray(parsed)) {
            rawEvents = parsed;
        }
        if (!Array.isArray(rawEvents)) return [];

        const events: NoteSequenceEvent[] = [];
        for (let index = 0; index < rawEvents.length; index++) {
            const raw = rawEvents[index];
            const bytes = copyBytes(raw.bytes);
            if (bytes.length === 0) continue;
            events.push({
                timeMs: Math.max(0, Math.floor(Number(raw.timeMs || 0))),
                label: String(raw.label || bytePreview(bytes, 8)),
                bytes: bytes
            });
        }
        events.sort(function (a: NoteSequenceEvent, b: NoteSequenceEvent): number {
            return a.timeMs - b.timeMs;
        });
        return events;
    } catch (error) {
        return [];
    }
}

function sequenceDurationMs(events: NoteSequenceEvent[]): number {
    let duration = 0;
    for (let index = 0; index < events.length; index++) {
        duration = Math.max(duration, events[index].timeMs);
    }
    return duration;
}

function formatSequenceDuration(ms: number): string {
    const safeMs = Math.max(0, Math.floor(ms));
    if (safeMs < 1000) return safeMs + ' ms';
    return (safeMs / 1000).toFixed(1) + ' s';
}

function sequenceId(sequence: any): number {
    return Number(sequence.id || 0);
}

function sequenceById(id: number): any {
    for (let index = 0; index < presetSequences.length; index++) {
        if (sequenceId(presetSequences[index]) === id) return presetSequences[index];
    }
    return null;
}

function sequenceMemoName(sequence: any): string {
    const id = sequenceId(sequence);
    for (let index = 0; index < presetSequences.length; index++) {
        if (sequenceId(presetSequences[index]) === id) return 'Memo ' + (index + 1);
    }
    return 'Memo';
}

function sequenceEventCount(sequence: any): number {
    const stored = Number(sequence.event_count || 0);
    if (stored > 0) return stored;
    return decodeNoteSequence(String(sequence.payload || '')).length;
}

function sequenceDurationForRow(sequence: any): number {
    const stored = Number(sequence.duration_ms || 0);
    if (stored > 0) return stored;
    return sequenceDurationMs(decodeNoteSequence(String(sequence.payload || '')));
}

function sequenceEventType(event: NoteSequenceEvent): number {
    if (!event.bytes || event.bytes.length === 0) return 0;
    const status = byteValue(event.bytes[0]);
    if (status >= 0x80 && status < 0xF0) return status & 0xF0;
    return status;
}

function sequenceEventChannel(event: NoteSequenceEvent): number {
    if (!event.bytes || event.bytes.length === 0) return 0;
    const status = byteValue(event.bytes[0]);
    if (status >= 0x80 && status < 0xF0) return status & 0x0F;
    return 0;
}

function sequenceEventNote(event: NoteSequenceEvent): number {
    if (!event.bytes || event.bytes.length < 2) return -1;
    return byteValue(event.bytes[1]);
}

function sequenceEventVelocity(event: NoteSequenceEvent): number {
    if (!event.bytes || event.bytes.length < 3) return 0;
    return byteValue(event.bytes[2]);
}

function isSequenceNoteOn(event: NoteSequenceEvent): boolean {
    if (sequenceEventType(event) !== sys.midi.NOTE_ON) return false;
    return sequenceEventVelocity(event) > 0;
}

function isSequenceNoteOff(event: NoteSequenceEvent): boolean {
    const type = sequenceEventType(event);
    if (type === sys.midi.NOTE_OFF) return true;
    if (type === sys.midi.NOTE_ON && sequenceEventVelocity(event) === 0) return true;
    return false;
}

function noteEventKey(event: NoteSequenceEvent): string {
    return String(sequenceEventChannel(event)) + ':' + String(sequenceEventNote(event));
}

/** The first time in `times` (ascending) after `after`, or -1. */
function nextTimeAfter(times: number[], after: number): number {
    for (let index = 0; index < times.length; index++) {
        if (times[index] > after) return times[index];
    }
    return -1;
}

function prepareSequencePlaybackEvents(events: NoteSequenceEvent[]): NoteSequenceEvent[] {
    const prepared = normalizedSequenceEvents(events);
    // When each key is struck: a lengthened note must end before its next strike.
    const strikes: Record<string, number[]> = {};
    for (let index = 0; index < prepared.length; index++) {
        const event = prepared[index];
        if (!isSequenceNoteOn(event)) continue;
        const key = noteEventKey(event);
        if (!strikes[key]) strikes[key] = [];
        strikes[key].push(event.timeMs);
    }

    const activeNotes: Record<string, number> = {};
    const held: Record<string, boolean> = {};
    const pedals: Record<string, boolean> = {};
    let endTime = 0;
    for (let index = 0; index < prepared.length; index++) {
        const event = prepared[index];
        const key = noteEventKey(event);
        if (isSequenceNoteOn(event)) {
            activeNotes[key] = event.timeMs;
        } else if (isSequenceNoteOff(event) && activeNotes[key] !== undefined) {
            const noteOnTime = activeNotes[key];
            if (event.timeMs - noteOnTime < MIN_PLAYBACK_NOTE_MS) {
                let target = noteOnTime + MIN_PLAYBACK_NOTE_MS;
                const nextStrike = nextTimeAfter(strikes[key] || [], noteOnTime);
                if (nextStrike >= 0 && nextStrike - 1 < target) target = Math.max(event.timeMs, nextStrike - 1);
                event.timeMs = target;
            }
            delete activeNotes[key];
        }
        trackHeld(event, held, pedals);
        endTime = Math.max(endTime, event.timeMs);
    }

    // Notes or pedals still down at the end (memos recorded before Stop
    // released them) are released when the memo ends.
    const releases = releaseEvents(held, pedals, endTime + MIN_PLAYBACK_NOTE_MS);
    for (let index = 0; index < releases.length; index++) prepared.push(releases[index]);

    prepared.sort(function (a: NoteSequenceEvent, b: NoteSequenceEvent): number {
        return a.timeMs - b.timeMs;
    });
    return prepared;
}

function savePresetSequence(presetId: number, events: NoteSequenceEvent[]): void {
    const normalized = normalizedSequenceEvents(events);
    if (presetId <= 0 || normalized.length === 0) return;

    const durationMs = sequenceDurationMs(normalized);
    sys.db.run(
        database,
        'INSERT INTO preset_sequences (preset_id, payload, event_count, duration_ms) VALUES (?, ?, ?, ?)',
        presetId,
        encodeNoteSequence(normalized),
        normalized.length,
        durationMs
    );
    loadPresetSequences();
    addLog('Saved sequence memo', normalized.length + ' events, ' + formatSequenceDuration(durationMs), COLORS.green);
}

/** Release what playback left sounding: notes and pedals still down. */
function releaseSequencePlayback(): void {
    if (sequencePlaying && outputHandle >= 0) {
        const releases = releaseEvents(sequencePlaybackHeld, sequencePlaybackPedals, 0);
        for (let index = 0; index < releases.length; index++) {
            sys.midi.sendRaw(outputHandle, releases[index].bytes);
        }
    }
    sequencePlaybackHeld = {};
    sequencePlaybackPedals = {};
}

function stopSequencePlayback(): void {
    releaseSequencePlayback();
    sequencePlaying = false;
    sequencePlayingId = 0;
    sequencePlaybackStartedAt = 0;
    sequencePlaybackEvents = [];
    sequencePlaybackIndex = 0;
    sequencePlaybackSent = 0;
    sequencePlaybackName = '';
}

function startSequenceRecording(): void {
    if (liveMode) return;
    if (selectedPresetId <= 0) {
        addLog('Select a preset before recording notes', '', COLORS.amber);
        return;
    }
    if (inputHandle < 0) {
        addLog('No MIDI input open', '', COLORS.amber);
        return;
    }

    stopSequencePlayback();
    sequenceRecording = true;
    sequenceRecordStartedAt = now();
    sequenceRecordMidiStartedAtUs = 0;
    sequenceRecordClockSet = false;
    sequenceRecordLastNativeUs = 0;
    sequenceRecordHeld = {};
    sequenceRecordPedals = {};
    sequenceRecordPresetId = selectedPresetId;
    sequenceRecordingEvents = [];
    addLog('Recording note sequence', 'Play notes, then hit Stop', COLORS.red);
}

function stopSequenceRecording(): void {
    if (!sequenceRecording) return;

    const presetId = sequenceRecordPresetId;
    const eventCount = sequenceRecordingEvents.length;
    sequenceRecording = false;
    sequenceRecordMidiStartedAtUs = 0;
    sequenceRecordPresetId = 0;

    if (eventCount === 0) {
        sequenceRecordingEvents = [];
        addLog('No notes recorded', '', COLORS.amber);
        return;
    }

    // Notes and pedals still held at Stop are released there, so the memo
    // never leaves a note hanging when it plays back.
    let stopTime = recordingTimeMs(0);
    for (let index = 0; index < sequenceRecordingEvents.length; index++) {
        stopTime = Math.max(stopTime, sequenceRecordingEvents[index].timeMs);
    }
    const releases = releaseEvents(sequenceRecordHeld, sequenceRecordPedals, stopTime);
    for (let index = 0; index < releases.length; index++) sequenceRecordingEvents.push(releases[index]);
    sequenceRecordHeld = {};
    sequenceRecordPedals = {};

    savePresetSequence(presetId, sequenceRecordingEvents);
    sequenceRecordingEvents = [];
}

function recordSequenceMidiMessage(message: MidiMessage): boolean {
    if (!sequenceRecording || liveMode) return false;
    const event = sequenceEventFromMidiMessage(message);
    if (!event) return false;

    // At the limit, releases are still taken so no note is left held.
    let limit = MAX_SEQUENCE_EVENTS;
    if (isReleaseEvent(event)) limit = MAX_SEQUENCE_EVENTS + MAX_SEQUENCE_RELEASE_EVENTS;
    if (sequenceRecordingEvents.length >= limit) {
        addLog('Sequence event limit reached; hit Stop', '', COLORS.amber);
        return true;
    }
    if (sequenceRecordingEvents.length >= MAX_SEQUENCE_EVENTS) {
        addLog('Sequence event limit reached; hit Stop', 'releasing held notes', COLORS.amber);
    }

    trackHeld(event, sequenceRecordHeld, sequenceRecordPedals);
    sequenceRecordingEvents.push(event);
    addLog('Recording ' + sequenceRecordingEvents.length + ' note events', '', COLORS.red);
    return true;
}

function playPresetSequence(sequenceIdToPlay: number): void {
    if (liveMode) return;
    if (sequenceRecording) {
        addLog('Stop recording before playback', '', COLORS.amber);
        return;
    }
    if (outputHandle < 0) {
        addLog('No MIDI output open', '', COLORS.red);
        return;
    }

    const sequence = sequenceById(sequenceIdToPlay);
    if (!sequence) {
        addLog('Select a memo to play', '', COLORS.amber);
        return;
    }

    const events = prepareSequencePlaybackEvents(decodeNoteSequence(String(sequence.payload || '')));
    if (events.length === 0) {
        addLog('Memo has no note events', '', COLORS.amber);
        return;
    }

    stopSequencePlayback();
    sequencePlaying = true;
    sequencePlayingId = sequenceIdToPlay;
    sequencePlaybackStartedAt = playbackNow();
    sequencePlaybackEvents = events;
    sequencePlaybackIndex = 0;
    sequencePlaybackSent = 0;
    sequencePlaybackName = sequenceMemoName(sequence);
    addLog('Playing ' + sequencePlaybackName, '', COLORS.green);
}

function finishSequencePlayback(): void {
    if (!sequencePlaying) return;
    const sent = sequencePlaybackSent;
    const total = sequencePlaybackEvents.length;
    const memoName = sequencePlaybackName;
    stopSequencePlayback();

    let color = COLORS.amber;
    if (sent === total) color = COLORS.green;
    addLog('Played ' + memoName, sent + ' / ' + total + ' events', color);
}

function updateSequencePlayback(): void {
    if (!sequencePlaying) return;
    if (outputHandle < 0) {
        finishSequencePlayback();
        return;
    }

    let elapsed = playbackNow() - sequencePlaybackStartedAt;
    // After a stall, carry on from here instead of sending everything overdue.
    if (sequencePlaybackIndex < sequencePlaybackEvents.length) {
        const late = elapsed - sequencePlaybackEvents[sequencePlaybackIndex].timeMs;
        if (late > PLAYBACK_MAX_LATE_MS) {
            sequencePlaybackStartedAt += late;
            elapsed -= late;
        }
    }
    while (sequencePlaybackIndex < sequencePlaybackEvents.length) {
        const event = sequencePlaybackEvents[sequencePlaybackIndex];
        if (event.timeMs > elapsed) break;
        if (sys.midi.sendRaw(outputHandle, event.bytes)) {
            sequencePlaybackSent += 1;
            trackHeld(event, sequencePlaybackHeld, sequencePlaybackPedals);
        }
        sequencePlaybackIndex += 1;
    }

    if (sequencePlaybackIndex >= sequencePlaybackEvents.length) {
        finishSequencePlayback();
    }
}

function deletePresetSequence(sequenceIdToDelete: number): void {
    const sequence = sequenceById(sequenceIdToDelete);
    if (selectedPresetId <= 0 || !sequence) {
        addLog('No memo to delete', '', COLORS.amber);
        return;
    }

    if (sequencePlayingId === sequenceIdToDelete) {
        stopSequencePlayback();
    }
    sys.db.run(database, 'DELETE FROM preset_sequences WHERE id = ? AND preset_id = ?', sequenceIdToDelete, selectedPresetId);
    loadPresetSequences();
    addLog('Deleted sequence memo', selectedPresetName(), COLORS.amber);
}

function createPreset(name: string): void {
    const commands = snapshotCommands();
    if (commands.length === 0) {
        addLog('No captured MIDI state to save', '', COLORS.amber);
    }
    if (selectedSongId <= 0) {
        addLog('Select a song first', '', COLORS.amber);
        return;
    }

    const orderRows = sys.db.query(database, 'SELECT COALESCE(MAX(sort_order), 0) + 1 AS next_order FROM presets WHERE song_id = ?', selectedSongId);
    let presetOrder = 1;
    if (orderRows.length > 0) {
        presetOrder = Number(orderRows[0].next_order || 1);
    }
    sys.db.run(
        database,
        'INSERT INTO presets (song_id, name, payload, command_count, sort_order) VALUES (?, ?, ?, ?, ?)',
        selectedSongId,
        sanitizeName(name, 'Preset'),
        encodePreset(commands),
        commands.length,
        presetOrder
    );
    selectedPresetId = sys.db.lastInsertId(database);
    loadPresets();
    addLog('Preset saved with ' + commands.length + ' commands', '', COLORS.green);
}

function updatePreset(id: number): void {
    const commands = snapshotCommands();
    if (id <= 0) {
        addLog('Select a preset to update', '', COLORS.amber);
        return;
    }
    if (commands.length === 0) {
        addLog('No captured MIDI state to save', '', COLORS.amber);
        return;
    }
    sys.db.run(
        database,
        "UPDATE presets SET payload = ?, command_count = ?, updated_at = datetime('now') WHERE id = ?",
        encodePreset(commands),
        commands.length,
        id
    );
    loadPresets();
    addLog('Preset updated with ' + commands.length + ' commands', '', COLORS.green);
}

function renamePreset(id: number, name: string): void {
    if (id <= 0) return;
    sys.db.run(database, "UPDATE presets SET name = ?, updated_at = datetime('now') WHERE id = ?", sanitizeName(name, 'Preset'), id);
    loadPresets();
    addLog('Preset renamed', '', COLORS.green);
}

function deletePreset(id: number): void {
    if (id <= 0) {
        addLog('Select a preset to delete', '', COLORS.amber);
        return;
    }

    let preset = null;
    for (let index = 0; index < presets.length; index++) {
        if (rowId(presets[index]) === id) preset = presets[index];
    }
    if (!preset) {
        addLog('Select a preset to delete', '', COLORS.amber);
        return;
    }

    const name = rowName(preset);
    stopSequencePlayback();
    sys.db.run(database, 'DELETE FROM preset_sequences WHERE preset_id = ?', id);
    sys.db.run(database, 'DELETE FROM presets WHERE id = ?', id);
    if (lastRecalledPresetId === id) lastRecalledPresetId = 0;
    if (selectedPresetId === id) selectedPresetId = 0;
    loadPresets();
    addLog('Deleted preset', name, COLORS.amber);
}

function commandCountForPreset(preset: any): number {
    const stored = Number(preset.command_count || 0);
    if (stored > 0) return stored;
    return decodePreset(String(preset.payload || '')).length;
}

/** A recall in progress: commands left to send, paced after each SysEx. */
let recallQueue: MidiCommand[] = [];
let recallQueueName = '';
let recallQueueSent = 0;
let recallQueueTotal = 0;
let recallResumeAt = 0;

/** The preset row `id`, from the selected song's presets or any song's. */
function presetRowById(id: number): any {
    for (let index = 0; index < presets.length; index++) {
        if (rowId(presets[index]) === id) return presets[index];
    }
    for (let index = 0; index < livePresets.length; index++) {
        if (rowId(livePresets[index]) === id) return livePresets[index];
    }
    return null;
}

function recallPreset(id: number): void {
    if (outputHandle < 0) {
        addLog('No MIDI output open', '', COLORS.red);
        lastRecallTime = now();
        return;
    }

    const preset = presetRowById(id);
    if (!preset) {
        addLog('Select a preset to recall', '', COLORS.amber);
        return;
    }

    const commands = decodePreset(String(preset.payload || ''));
    if (commands.length === 0) {
        addLog('Preset has no MIDI commands', '', COLORS.amber);
        lastRecallTime = now();
        return;
    }

    // Send first; a recall still in progress is replaced by this one.
    recallQueue = commands;
    recallQueueName = rowName(preset);
    recallQueueSent = 0;
    recallQueueTotal = commands.length;
    recallResumeAt = 0;
    selectedPresetId = id;
    lastRecalledPresetId = id;
    lastRecallTime = now();
    pumpRecall();
    // Then remember the selection (a database write, after the MIDI is out).
    saveAppState('selected_preset_id', String(selectedPresetId));
}

/** Send the recall's next commands: channel messages back to back, then a
 *  pause after each SysEx. Runs each frame until the queue is empty. */
function pumpRecall(): void {
    if (recallQueue.length === 0) return;
    if (outputHandle < 0) {
        addLog('Recall interrupted: no MIDI output', recallQueueName, COLORS.red);
        recallQueue = [];
        return;
    }
    while (recallQueue.length > 0 && playbackNow() >= recallResumeAt) {
        const command = recallQueue.shift();
        if (sys.midi.sendRaw(outputHandle, command.bytes)) recallQueueSent += 1;
        if (command.bytes[0] === 0xF0) recallResumeAt = playbackNow() + RECALL_SYSEX_GAP_MS;
    }
    if (recallQueue.length > 0) return;
    let sentColor = COLORS.amber;
    if (recallQueueSent === recallQueueTotal) sentColor = COLORS.green;
    addLog('Recalled ' + recallQueueName, recallQueueSent + ' / ' + recallQueueTotal + ' commands', sentColor);
}

function closeMidiHandles(): void {
    // Release playing notes and stop a paced recall on the port being closed.
    stopSequencePlayback();
    recallQueue = [];
    if (inputHandle >= 0) {
        sys.midi.closeInput(inputHandle);
        inputHandle = -1;
    }
    if (outputHandle >= 0) {
        sys.midi.closeOutput(outputHandle);
        outputHandle = -1;
    }
}

function openMidiHandles(): void {
    closeMidiHandles();
    if (!midiAvailable) {
        addLog('MIDI is unavailable on this platform', '', COLORS.red);
        return;
    }

    // (Written `length > index`: Budo's TypeScript stripper can read `a < b` as a generic.)
    if (selectedInputIndex >= 0 && inputDevices.length > selectedInputIndex) {
        inputHandle = sys.midi.openInput(selectedInputIndex, function (message: MidiMessage): void {
            const recordedSequenceEvent = recordSequenceMidiMessage(message);
            const command = commandFromMidiMessage(message);
            if (command) {
                captureCommand(command);
                let logColor = COLORS.teal;
                if (command.key.indexOf('sysex:') === 0) {
                    logColor = COLORS.amber;
                }
                addLog(command.label, bytePreview(command.bytes, 12), logColor);
            } else if (!recordedSequenceEvent) {
                const typeName = messageTypeName(Number(message.type || 0));
                addLog(typeName + ' ch ' + channelLabel(Number(message.channel || 0)), 'not stored as preset state', COLORS.muted);
            }
        });
    }

    if (selectedOutputIndex >= 0 && outputDevices.length > selectedOutputIndex) {
        outputHandle = sys.midi.openOutput(selectedOutputIndex);
    }

    let inputName = 'none';
    if (inputHandle >= 0) inputName = selectedInputName;
    let deviceColor = COLORS.amber;
    if (outputHandle >= 0) {
        deviceColor = COLORS.green;
    }
    let deviceStatus = 'Ready - input: ' + inputName;
    if (inputDevices.length === 0 && outputDevices.length === 0) {
        deviceStatus = 'Connect a MIDI device - changes are detected automatically';
    } else if (selectedOutputName.length > 0 && selectedOutputIndex < 0) {
        // The chosen output is gone: never fall back to another device.
        deviceColor = COLORS.red;
        deviceStatus = selectedOutputName + ' disconnected - Live recall waits for it';
    } else if (outputHandle < 0) {
        deviceStatus = 'Select a MIDI output to enable Live recall';
    }
    addLog(deviceStatus, '', deviceColor);
}

/** Position of the device called `name`, or -1. */
function deviceIndexByName(devices: MidiDevice[], name: string): number {
    for (let index = 0; index < devices.length; index++) {
        if (devices[index].name === name) return index;
    }
    return -1;
}

function chooseInput(index: number): void {
    if (0 > index || index >= inputDevices.length) return;
    selectedInputIndex = index;
    selectedInputName = inputDevices[index].name;
    saveAppState('midi_input_name', selectedInputName);
    openMidiHandles();
}

function chooseOutput(index: number): void {
    if (0 > index || index >= outputDevices.length) return;
    selectedOutputIndex = index;
    selectedOutputName = outputDevices[index].name;
    saveAppState('midi_output_name', selectedOutputName);
    openMidiHandles();
}

function refreshMidiDevices(): void {
    if (!midiAvailable) {
        addLog('MIDI is unavailable on this platform', '', COLORS.red);
        return;
    }
    sys.midi.refreshDevices();
    inputDevices = sys.midi.getInputDevices();
    outputDevices = sys.midi.getOutputDevices();
    if (!deviceNamesLoaded) {
        selectedInputName = loadAppState('midi_input_name', '');
        selectedOutputName = loadAppState('midi_output_name', '');
        deviceNamesLoaded = true;
    }
    // Devices are found again by name: after a hot-plug the list positions
    // shift, and a recall must never go to a different device.
    selectedInputIndex = deviceIndexByName(inputDevices, selectedInputName);
    if (selectedInputName.length === 0 && inputDevices.length > 0) {
        selectedInputIndex = 0;
        selectedInputName = inputDevices[0].name;
    }
    selectedOutputIndex = deviceIndexByName(outputDevices, selectedOutputName);
    if (selectedOutputName.length === 0 && outputDevices.length > 0) {
        selectedOutputIndex = 0;
        selectedOutputName = outputDevices[0].name;
    }
    openMidiHandles();
}

function handleMidiDevicesChanged(event: MidiDevicesChangedEvent): void {
    refreshMidiDevices();
    addLog('MIDI devices changed', event.inputs.length + ' inputs / ' + event.outputs.length + ' outputs', COLORS.blue);
}

function requestKeepScreenOn(): void {
    const accepted = sys.device.keepScreenOn(true);
    if (accepted) {
        addLog('Screen sleep disabled', 'sys.device.keepScreenOn(true)', COLORS.green);
    } else {
        addLog('Screen sleep request refused', 'sys.device.keepScreenOn(true)', COLORS.amber);
    }
}

function selectInput(delta: number): void {
    if (inputDevices.length === 0) return;
    chooseInput(wrapIndex(selectedInputIndex + delta, inputDevices.length));
}

function selectOutput(delta: number): void {
    if (outputDevices.length === 0) return;
    chooseOutput(wrapIndex(selectedOutputIndex + delta, outputDevices.length));
}

function buildDatabaseExport(): any {
    let selectedSong: number | null = null;
    let selectedPreset: number | null = null;
    if (selectedSongId > 0) selectedSong = selectedSongId;
    if (selectedPresetId > 0) selectedPreset = selectedPresetId;
    const document: any = {
        $schema: 'midi-preset-saver.schema.json',
        format: 'midi-preset-saver',
        version: 1,
        exportedAt: new Date().toISOString(),
        songs: [],
        selection: { songId: selectedSong, presetId: selectedPreset }
    };
    const songRows = sys.db.query(database, 'SELECT * FROM songs ORDER BY sort_order ASC, id ASC');
    for (let songIndex = 0; songIndex < songRows.length; songIndex++) {
        const songRow = songRows[songIndex];
        const song: any = {
            id: Number(songRow.id),
            name: String(songRow.name || ''),
            sortOrder: Number(songRow.sort_order || 0),
            createdAt: String(songRow.created_at || ''),
            presets: []
        };
        const presetRows = sys.db.query(database,
            'SELECT * FROM presets WHERE song_id = ? ORDER BY sort_order ASC, id ASC', song.id);
        for (let presetIndex = 0; presetIndex < presetRows.length; presetIndex++) {
            const presetRow = presetRows[presetIndex];
            const preset: any = {
                id: Number(presetRow.id),
                name: String(presetRow.name || ''),
                sortOrder: Number(presetRow.sort_order || 0),
                createdAt: String(presetRow.created_at || ''),
                updatedAt: String(presetRow.updated_at || ''),
                commands: decodePreset(String(presetRow.payload || '')),
                sequences: []
            };
            const sequenceRows = sys.db.query(database,
                'SELECT * FROM preset_sequences WHERE preset_id = ? ORDER BY id ASC', preset.id);
            for (let sequenceIndex = 0; sequenceIndex < sequenceRows.length; sequenceIndex++) {
                const sequenceRow = sequenceRows[sequenceIndex];
                preset.sequences.push({
                    id: Number(sequenceRow.id),
                    createdAt: String(sequenceRow.created_at || ''),
                    updatedAt: String(sequenceRow.updated_at || ''),
                    events: decodeNoteSequence(String(sequenceRow.payload || ''))
                });
            }
            song.presets.push(preset);
        }
        document.songs.push(song);
    }
    return document;
}

function exportTimestamp(): string {
    const date = new Date();
    function pad(value: number): string {
        return (value < 10 ? '0' : '') + value;
    }
    return date.getFullYear() + pad(date.getMonth() + 1) + pad(date.getDate()) + '_' +
        pad(date.getHours()) + pad(date.getMinutes()) + pad(date.getSeconds());
}

function exportDatabase(): void {
    try {
        const text = JSON.stringify(buildDatabaseExport(), null, 2);
        if (text.length > MAX_EXPORT_BYTES) {
            addLog('Export is larger than 64 MiB', 'Load will refuse it; remove large SysEx presets first', COLORS.red);
        }
        const filename = 'midi_preset_saver_' + exportTimestamp() + '.json';
        const launched = sys.files.saveText(filename, text, function (error: string | null): void {
            if (error) {
                if (error === 'File save cancelled') {
                    addLog('Export cancelled', '', COLORS.muted);
                    return;
                }
                addLog('Database export failed', error, COLORS.red);
                return;
            }
            addLog('Database exported', filename, COLORS.green);
        });
        if (!launched) throw new Error('System save dialog is unavailable');
    } catch (error) {
        addLog('Database export failed', String(error), COLORS.red);
    }
}

function exportedJsonFiles(): FileEntry[] {
    const entries = sys.files.list('files') || [];
    const result: FileEntry[] = [];
    for (let index = 0; index < entries.length; index++) {
        const entry = entries[index];
        if (entry.type !== 'file') continue;
        if (entry.name.indexOf('midi_preset_saver_') !== 0) continue;
        if (entry.name.slice(-5).toLowerCase() !== '.json') continue;
        result.push(entry);
    }
    result.sort(function (a, b) { return b.name.localeCompare(a.name); });
    return result;
}

function openImportDialog(): void {
    importDialog.files = exportedJsonFiles();
    importDialog.selected = 0;
    importDialog.error = importDialog.files.length === 0
        ? 'No exports found in files/'
        : '';
    importDialog.active = true;
}

function closeImportDialog(): void {
    importDialog.active = false;
    importDialog.files = [];
    importDialog.selected = 0;
    importDialog.error = '';
}

function requireExportObject(value: any, path: string): void {
    if (!value || typeof value !== 'object' || Array.isArray(value)) {
        throw new Error(path + ' must be an object');
    }
}

function requireExportArray(value: any, path: string): void {
    if (!Array.isArray(value)) throw new Error(path + ' must be an array');
}

function requireExportString(value: any, path: string, maximum: number): void {
    if (typeof value !== 'string' || value.length === 0 || value.length > maximum) {
        throw new Error(path + ' must be a non-empty string');
    }
}

function requireExportInteger(value: any, path: string, minimum: number): void {
    if (typeof value !== 'number' || !isFinite(value) || Math.floor(value) !== value || value < minimum) {
        throw new Error(path + ' must be an integer greater than or equal to ' + minimum);
    }
}

function validateExportBytes(value: any, path: string): void {
    requireExportArray(value, path);
    if (value.length === 0) throw new Error(path + ' must not be empty');
    for (let index = 0; index < value.length; index++) {
        requireExportInteger(value[index], path + '[' + index + ']', 0);
        if (value[index] > 255) throw new Error(path + '[' + index + '] must not exceed 255');
    }
}

function validateDatabaseExport(text: string): any {
    if (text.length === 0 || text.length > MAX_EXPORT_BYTES) {
        throw new Error('Export file is empty or larger than 64 MiB');
    }
    let document: any = null;
    try {
        document = JSON.parse(text);
    } catch (error) {
        throw new Error('Export is not valid JSON');
    }
    requireExportObject(document, 'document');
    if (document.$schema !== 'midi-preset-saver.schema.json' ||
        document.format !== 'midi-preset-saver' || document.version !== 1) {
        throw new Error('Not a supported MIDI Preset Saver JSON export');
    }
    requireExportString(document.exportedAt, 'exportedAt', 64);
    requireExportArray(document.songs, 'songs');
    requireExportObject(document.selection, 'selection');

    const songIds: Record<string, boolean> = {};
    const presetIds: Record<string, boolean> = {};
    const sequenceIds: Record<string, boolean> = {};
    for (let songIndex = 0; songIndex < document.songs.length; songIndex++) {
        const song = document.songs[songIndex];
        const songPath = 'songs[' + songIndex + ']';
        requireExportObject(song, songPath);
        requireExportInteger(song.id, songPath + '.id', 1);
        requireExportString(song.name, songPath + '.name', 48);
        requireExportInteger(song.sortOrder, songPath + '.sortOrder', 0);
        requireExportString(song.createdAt, songPath + '.createdAt', 64);
        requireExportArray(song.presets, songPath + '.presets');
        if (songIds[String(song.id)]) throw new Error('Duplicate song id ' + song.id);
        songIds[String(song.id)] = true;

        for (let presetIndex = 0; presetIndex < song.presets.length; presetIndex++) {
            const preset = song.presets[presetIndex];
            const presetPath = songPath + '.presets[' + presetIndex + ']';
            requireExportObject(preset, presetPath);
            requireExportInteger(preset.id, presetPath + '.id', 1);
            requireExportString(preset.name, presetPath + '.name', 48);
            requireExportInteger(preset.sortOrder, presetPath + '.sortOrder', 0);
            requireExportString(preset.createdAt, presetPath + '.createdAt', 64);
            requireExportString(preset.updatedAt, presetPath + '.updatedAt', 64);
            requireExportArray(preset.commands, presetPath + '.commands');
            requireExportArray(preset.sequences, presetPath + '.sequences');
            if (presetIds[String(preset.id)]) throw new Error('Duplicate preset id ' + preset.id);
            presetIds[String(preset.id)] = true;

            for (let commandIndex = 0; commandIndex < preset.commands.length; commandIndex++) {
                const command = preset.commands[commandIndex];
                const commandPath = presetPath + '.commands[' + commandIndex + ']';
                requireExportObject(command, commandPath);
                requireExportString(command.key, commandPath + '.key', 512);
                requireExportString(command.label, commandPath + '.label', 512);
                validateExportBytes(command.bytes, commandPath + '.bytes');
                if (typeof command.receivedAt !== 'number' || !isFinite(command.receivedAt) || command.receivedAt < 0) {
                    throw new Error(commandPath + '.receivedAt must be a non-negative number');
                }
            }

            for (let sequenceIndex = 0; sequenceIndex < preset.sequences.length; sequenceIndex++) {
                const sequence = preset.sequences[sequenceIndex];
                const sequencePath = presetPath + '.sequences[' + sequenceIndex + ']';
                requireExportObject(sequence, sequencePath);
                requireExportInteger(sequence.id, sequencePath + '.id', 1);
                requireExportString(sequence.createdAt, sequencePath + '.createdAt', 64);
                requireExportString(sequence.updatedAt, sequencePath + '.updatedAt', 64);
                requireExportArray(sequence.events, sequencePath + '.events');
                if (sequenceIds[String(sequence.id)]) throw new Error('Duplicate sequence id ' + sequence.id);
                sequenceIds[String(sequence.id)] = true;
                for (let eventIndex = 0; eventIndex < sequence.events.length; eventIndex++) {
                    const event = sequence.events[eventIndex];
                    const eventPath = sequencePath + '.events[' + eventIndex + ']';
                    requireExportObject(event, eventPath);
                    requireExportInteger(event.timeMs, eventPath + '.timeMs', 0);
                    requireExportString(event.label, eventPath + '.label', 512);
                    validateExportBytes(event.bytes, eventPath + '.bytes');
                }
            }
        }
    }

    const selection = document.selection;
    if (selection.songId !== null) {
        requireExportInteger(selection.songId, 'selection.songId', 1);
        if (!songIds[String(selection.songId)]) throw new Error('Selected song does not exist');
    }
    if (selection.presetId !== null) {
        requireExportInteger(selection.presetId, 'selection.presetId', 1);
        if (!presetIds[String(selection.presetId)]) throw new Error('Selected preset does not exist');
    }
    return document;
}

function reloadImportedDatabase(): void {
    ensurePresetSequencesAllowMultiple();
    ensurePresetsHaveSortOrder();
    ensureDefaultSong();
    selectedSongId = Number(loadAppState('selected_song_id', '0'));
    selectedPresetId = Number(loadAppState('selected_preset_id', '0'));
    songScroll = 0;
    presetScroll = 0;
    sequenceScroll = 0;
    stopSequencePlayback();
    loadSongs();
}

function applyDatabaseExport(document: any): void {
    if (!sys.db.execute(database, 'BEGIN IMMEDIATE TRANSACTION')) {
        throw new Error(sys.db.getError() || 'Could not start import transaction');
    }
    try {
        sys.db.run(database, 'DELETE FROM app_state');
        sys.db.run(database, 'DELETE FROM preset_sequences');
        sys.db.run(database, 'DELETE FROM presets');
        sys.db.run(database, 'DELETE FROM songs');
        for (let songIndex = 0; songIndex < document.songs.length; songIndex++) {
            const song = document.songs[songIndex];
            sys.db.run(database,
                'INSERT INTO songs (id, name, sort_order, created_at) VALUES (?, ?, ?, ?)',
                song.id, song.name, song.sortOrder, song.createdAt);
            for (let presetIndex = 0; presetIndex < song.presets.length; presetIndex++) {
                const preset = song.presets[presetIndex];
                sys.db.run(database,
                    'INSERT INTO presets (id, song_id, name, payload, command_count, sort_order, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
                    preset.id, song.id, preset.name, encodePreset(preset.commands), preset.commands.length,
                    preset.sortOrder, preset.createdAt, preset.updatedAt);
                for (let sequenceIndex = 0; sequenceIndex < preset.sequences.length; sequenceIndex++) {
                    const sequence = preset.sequences[sequenceIndex];
                    sys.db.run(database,
                        'INSERT INTO preset_sequences (id, preset_id, payload, event_count, duration_ms, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
                        sequence.id, preset.id, encodeNoteSequence(sequence.events), sequence.events.length,
                        sequenceDurationMs(sequence.events), sequence.createdAt, sequence.updatedAt);
                }
            }
        }
        if (document.selection.songId !== null) {
            sys.db.run(database, 'INSERT INTO app_state (key, value) VALUES (?, ?)',
                'selected_song_id', String(document.selection.songId));
        }
        if (document.selection.presetId !== null) {
            sys.db.run(database, 'INSERT INTO app_state (key, value) VALUES (?, ?)',
                'selected_preset_id', String(document.selection.presetId));
        }
        if (!sys.db.execute(database, 'COMMIT')) {
            throw new Error(sys.db.getError() || 'Could not commit imported data');
        }
    } catch (error) {
        try { sys.db.execute(database, 'ROLLBACK'); } catch (rollbackError) { }
        throw error;
    }
}

function importDatabaseJson(name: string, text: string): void {
    try {
        const document = validateDatabaseExport(text);
        applyDatabaseExport(document);
        reloadImportedDatabase();
        closeImportDialog();
        addLog('Database imported', name, COLORS.green);
    } catch (error) {
        importDialog.error = 'Import failed: ' + String(error);
        addLog('Database import failed', name + ': ' + String(error), COLORS.red);
    }
}

function importDatabaseFile(entry: FileEntry): void {
    const text = sys.files.readText('files/' + entry.name);
    if (text === null) {
        importDialog.error = sys.files.getError() || 'Could not read export';
        return;
    }
    importDatabaseJson(entry.name, text);
}

function browseDatabaseImport(): void {
    const launched = sys.files.pickText(function (file: PickedTextFile | null, error: string | null): void {
        if (file) {
            importDatabaseJson(file.name, file.text);
            return;
        }
        if (error && error !== 'File selection cancelled') importDialog.error = error;
    }, '.json');
    if (!launched) importDialog.error = 'System file picker is unavailable';
}

function openEditor(title: string, value: string, action: string, targetId: number): void {
    editor.active = true;
    editor.title = title;
    editor.value = value;
    editor.caret = value.length;
    editor.compositionText = '';
    editor.compositionCaret = 0;
    editor.blinkStarted = sys.input.get().totalTime;
    editor.action = action;
    editor.targetId = targetId;
    // The dialog's field takes the focus (and starts the text session) when drawn.
    editor.needsFocus = true;
}

function closeEditor(): void {
    if (editor.active) sys.input.stopTextInput();
    editor.active = false;
    editor.title = '';
    editor.value = '';
    editor.caret = 0;
    editor.compositionText = '';
    editor.compositionCaret = 0;
    editor.action = '';
    editor.targetId = 0;
}

function confirmEditor(): void {
    if (editor.compositionText.length > 0) {
        const available = Math.max(0, 48 - editor.value.length);
        const committed = editor.compositionText.slice(0, available);
        editor.value = editor.value.slice(0, editor.caret) + committed + editor.value.slice(editor.caret);
        editor.caret += committed.length;
        editor.compositionText = '';
        editor.compositionCaret = 0;
    }
    let fallback = 'Preset';
    if (editor.action === 'newSong' || editor.action === 'renameSong') {
        fallback = 'Song';
    }
    const value = sanitizeName(editor.value, fallback);
    const action = editor.action;
    const targetId = editor.targetId;
    closeEditor();

    if (action === 'newSong') createSong(value);
    if (action === 'renameSong') renameSong(targetId, value);
    if (action === 'newPreset') createPreset(value);
    if (action === 'renamePreset') renamePreset(targetId, value);
}

function openPresetMenu(presetId: number): void {
    if (presetId <= 0 || liveMode || sequenceRecording) return;
    if (selectedPresetId !== presetId) {
        stopSequencePlayback();
        selectedPresetId = presetId;
        saveAppState('selected_preset_id', String(selectedPresetId));
        loadPresetSequences();
    }
    presetMenu.active = true;
    presetMenu.presetId = presetId;
}

function closePresetMenu(): void {
    presetMenu.active = false;
    presetMenu.presetId = 0;
}

function openSongMenu(songId: number): void {
    if (songId <= 0 || liveMode || sequenceRecording) return;
    if (selectedSongId !== songId) {
        stopSequencePlayback();
        selectedSongId = songId;
        selectedPresetId = 0;
        presetScroll = 0;
        saveAppState('selected_song_id', String(selectedSongId));
        loadPresets();
    }
    songMenu.active = true;
    songMenu.songId = songId;
}

function closeSongMenu(): void {
    songMenu.active = false;
    songMenu.songId = 0;
}

// ── Interface (budo-ui) ──────────────────────────────────────────────────
//
// Everything below draws the interface with budo-ui (ui/). The data model,
// MIDI, sequences, and the JSON export/import above are independent of it.

const ui = createUI({
    background: COLORS.bg, surface: COLORS.panel, raised: COLORS.panelSoft, ink: COLORS.text,
    muted: COLORS.muted, border: COLORS.border, accent: COLORS.teal, accentInk: COLORS.black,
    highlight: COLORS.amber, danger: COLORS.red, focus: COLORS.teal, shadow: '#00000099',
    font: 15, small: 12, row: 44, radius: 6, gap: 8, padding: 12
});
ui.registerIcon('play', 'M8 5.5v13l10.5-6.5z', { fill: true });
ui.registerIcon('record', 'M17 12a5 5 0 1 1-10 0a5 5 0 1 1 10 0z', { fill: true });
ui.registerIcon('stop', 'M7 7h10v10H7z', { fill: true });

/** A touch must move this far before it scrolls instead of tapping (stage tremor). */
const TOUCH_MOVE_DP = 18;
/** Live recall flashes the recalled preset for this long. */
const RECALL_FLASH_MS = 700;

const LIVE_SONG_COLORS = [
    COLORS.teal,
    COLORS.blue,
    COLORS.amber,
    COLORS.songAccentRose,
    COLORS.green,
    COLORS.songAccentCopper
];

let sequencesExpanded = false;
/** True while the Edit/Live transition runs: input is ignored until it settles. */
let interfaceLocked = false;
/** Where the current press started, to cancel a Live recall that moved. */
let pressX = 0;
let pressY = 0;
let importSelected = 0;

function liveSongColor(songId: number): string {
    return LIVE_SONG_COLORS[Math.abs(songId) % LIVE_SONG_COLORS.length];
}

function editPresetsForSong(songId: number): any[] {
    const result: any[] = [];
    for (let index = 0; index < livePresets.length; index++) {
        if (Number(livePresets[index].live_song_id || livePresets[index].song_id || 0) === songId) {
            result.push(livePresets[index]);
        }
    }
    return result;
}

function fitText(text: string, width: number, size: number): string {
    if (ui.textWidth(text, size) <= width) return text;
    let end = text.length;
    while (end > 0 && ui.textWidth(text.slice(0, end) + '...', size) > width) end--;
    if (end <= 0) return '';
    return text.slice(0, end) + '...';
}

function textAt(text: string, x: number, y: number, size: number, color: string): void {
    ui.text(text, x, y + size, size, color);
}

function panel(rect: any, title: string): void {
    ui.fill(rect, COLORS.panel, ui.dp(7));
    ui.outline(rect, COLORS.border, 1, ui.dp(7));
    if (title.length > 0) textAt(title, rect.x + ui.dp(14), rect.y + ui.dp(10), ui.sp(15), COLORS.text);
}

// ── Actions shared by Edit and Live ──────────────────────────────────────

function selectSong(songId: number): void {
    if (sequenceRecording) {
        addLog('Hit Stop before changing songs', '', COLORS.amber);
        return;
    }
    stopSequencePlayback();
    selectedSongId = songId;
    selectedPresetId = 0;
    saveAppState('selected_song_id', String(selectedSongId));
    loadPresets();
}

function selectPreset(songId: number, presetId: number): void {
    if (sequenceRecording) {
        addLog('Hit Stop before changing presets', '', COLORS.amber);
        return;
    }
    stopSequencePlayback();
    selectedSongId = songId;
    selectedPresetId = presetId;
    saveAppState('selected_song_id', String(selectedSongId));
    loadPresets();
    selectedPresetId = presetId;
    saveAppState('selected_preset_id', String(selectedPresetId));
    loadPresetSequences();
}

function recallLivePreset(songId: number, presetId: number): void {
    if (sequenceRecording) {
        addLog('Hit Stop before changing presets', '', COLORS.amber);
        return;
    }
    if (songId <= 0 || presetId <= 0) return;
    // MIDI first (on stage, every millisecond counts), then the selection and
    // its database writes.
    stopSequencePlayback();
    recallPreset(presetId);
    selectPreset(songId, presetId);
}

function showSongMenu(songId: number): void {
    if (songId <= 0) return;
    selectedSongId = songId;
    selectedPresetId = 0;
    presetScroll = 0;
    saveAppState('selected_song_id', String(selectedSongId));
    loadPresets();
    openSongMenu(songId);
}

function showPresetMenu(songId: number, presetId: number): void {
    selectedSongId = songId;
    selectedPresetId = presetId;
    saveAppState('selected_song_id', String(selectedSongId));
    loadPresets();
    selectedPresetId = presetId;
    saveAppState('selected_preset_id', String(selectedPresetId));
    loadPresetSequences();
    openPresetMenu(presetId);
}

function toggleMode(): void {
    if (sequenceRecording) {
        addLog('Hit Stop before switching modes', '', COLORS.amber);
        return;
    }
    stopSequencePlayback();
    liveMode = !liveMode;
}

/** Move song `fromId` to the place of song `toId`, then store the order. */
function moveSong(fromId: number, toId: number): void {
    const from = indexOfRowId(songs, fromId);
    const to = indexOfRowId(songs, toId);
    if (from < 0 || to < 0) return;
    if (from === to) return;
    moveInArray(songs, from, to);
    persistSongOrder();
    loadSongs();
    addLog('Reordered song group', 'New position ' + (to + 1), COLORS.green);
}

/** Move preset `fromId` to the place of its sibling `toId`, then store the order. */
function movePreset(songId: number, fromId: number, toId: number): void {
    const siblings = editPresetsForSong(songId);
    const from = indexOfRowId(siblings, fromId);
    const to = indexOfRowId(siblings, toId);
    if (from < 0 || to < 0) return;
    if (from === to) return;
    moveInArray(siblings, from, to);
    // persistPresetOrderForSong reads the order from livePresets.
    let replacement = 0;
    for (let index = 0; index < livePresets.length; index++) {
        const rowSongId = Number(livePresets[index].live_song_id || livePresets[index].song_id || 0);
        if (rowSongId === songId && replacement < siblings.length) {
            livePresets[index] = siblings[replacement];
            replacement++;
        }
    }
    persistPresetOrderForSong(songId);
    loadPresets();
    addLog('Reordered preset', 'New position ' + (to + 1), COLORS.green);
}

// ── Header ───────────────────────────────────────────────────────────────

function drawHeader(rect: any, live: number): void {
    const dp = ui.dp;
    const modeRect = { x: rect.x + rect.width - dp(76), y: rect.y, width: dp(76), height: rect.height };
    textAt('MIDI Preset Saver', rect.x, rect.y + (rect.height - ui.sp(23)) / 2 - dp(3), ui.sp(23), COLORS.text);

    let modeLabel = 'Live';
    let modeVariant = 'primary';
    if (liveMode) {
        modeLabel = 'Setup';
        modeVariant = 'secondary';
    }
    const modeEnabled = !sequenceRecording && !interfaceLocked;
    if (ui.button('mode', modeLabel, modeRect, { variant: modeVariant, enabled: modeEnabled })) toggleMode();

    // Edit: data actions. Live: the output readiness dot.
    if (live < 0.999) {
        const actionW = dp(84);
        const saveRect = { x: modeRect.x - dp(6) - actionW, y: rect.y, width: actionW, height: rect.height };
        const loadRect = { x: saveRect.x - dp(6) - actionW, y: rect.y, width: actionW, height: rect.height };
        ui.layer(rect, { opacity: 1 - live }, () => {
            const enabled = !sequenceRecording && !interfaceLocked && !liveMode;
            if (ui.button('db:import', 'Load', loadRect, { enabled: enabled }) && enabled) openImportDialog();
            if (ui.button('db:export', 'Save', saveRect, { enabled: enabled }) && enabled) exportDatabase();
        });
    }
    if (live > 0.001) {
        let dotColor = COLORS.red;
        if (liveConnectionOk()) dotColor = COLORS.green;
        const dot = { x: modeRect.x - dp(20), y: rect.y + rect.height / 2 - dp(6), width: dp(12), height: dp(12) };
        ui.layer(dot, { opacity: live }, () => ui.fill(dot, dotColor, dp(6)));
    }
}

// ── Edit mode ────────────────────────────────────────────────────────────

function deviceOptions(devices: MidiDevice[]): any[] {
    const options: any[] = [];
    for (let index = 0; index < devices.length; index++) {
        options.push({ value: String(index), label: devices[index].name });
    }
    return options;
}

function drawDevices(rect: any): void {
    const columns = ui.columns(rect, [{ weight: 1 }, { weight: 1 }], ui.dp(8));
    const inputs = deviceOptions(inputDevices);
    const outputs = deviceOptions(outputDevices);
    // A list of one is still worth opening when the chosen device is missing.
    const inputEnabled = (inputDevices.length > 1 || (inputDevices.length > 0 && selectedInputIndex < 0)) && !sequenceRecording && !interfaceLocked;
    const outputEnabled = (outputDevices.length > 1 || (outputDevices.length > 0 && selectedOutputIndex < 0)) && !sequenceRecording && !interfaceLocked;
    // A chosen device that is unplugged keeps its name, marked disconnected.
    let inputPlaceholder = 'No input';
    if (selectedInputIndex < 0 && selectedInputName.length > 0) inputPlaceholder = selectedInputName + ' (disconnected)';
    let outputPlaceholder = 'No output';
    if (selectedOutputIndex < 0 && selectedOutputName.length > 0) outputPlaceholder = selectedOutputName + ' (disconnected)';
    const pickedInput = ui.select('input', columns[0], inputs, String(selectedInputIndex),
        { label: 'MIDI input', placeholder: inputPlaceholder, enabled: inputEnabled });
    const pickedOutput = ui.select('output', columns[1], outputs, String(selectedOutputIndex),
        { label: 'MIDI output', placeholder: outputPlaceholder, enabled: outputEnabled });
    if (pickedInput !== null && Number(pickedInput) !== selectedInputIndex) chooseInput(Number(pickedInput));
    if (pickedOutput !== null && Number(pickedOutput) !== selectedOutputIndex) chooseOutput(Number(pickedOutput));
}

function libraryItems(): any[] {
    const dp = ui.dp;
    const items: any[] = [];
    for (let songIndex = 0; songIndex < songs.length; songIndex++) {
        const song = songs[songIndex];
        const songId = rowId(song);
        const children = editPresetsForSong(songId);
        items.push({
            id: 's:' + songId, kind: 'song', groupId: 'songs', title: rowName(song), height: dp(50),
            action: true, actionLabel: '•••', actionName: 'Song actions, ' + rowName(song),
            draggable: songs.length > 1
        });
        if (children.length === 0) {
            items.push({ id: 'e:' + songId, kind: 'empty', groupId: 'empty:' + songId, title: 'No presets',
                depth: 1, height: dp(34), draggable: false });
        }
        for (let presetIndex = 0; presetIndex < children.length; presetIndex++) {
            const preset = children[presetIndex];
            items.push({
                id: 'p:' + songId + ':' + rowId(preset), kind: 'preset', groupId: 'song:' + songId,
                title: rowName(preset), subtitle: commandCountForPreset(preset) + ' commands',
                depth: 1, height: dp(48), action: true, actionLabel: '•••',
                actionName: 'Preset actions, ' + rowName(preset), draggable: children.length > 1
            });
        }
    }
    return items;
}

function drawLibrary(rect: any): void {
    const dp = ui.dp;
    panel(rect, 'Presets');
    const buttonH = dp(36);
    const footerH = dp(22);
    const listRect = {
        x: rect.x + dp(8), y: rect.y + dp(38), width: rect.width - dp(16),
        height: Math.max(dp(60), rect.height - dp(38) - buttonH - footerH - dp(16))
    };
    let selected = 's:' + selectedSongId;
    if (selectedPresetId > 0) selected = 'p:' + selectedSongId + ':' + selectedPresetId;
    const result = ui.list('library', listRect, libraryItems(), {
        selectedId: selected, reorder: !sequenceRecording, indent: dp(22),
        dragThreshold: TOUCH_MOVE_DP, emptyText: 'No songs'
    });
    if (!interfaceLocked) handleLibraryResult(result);

    const buttonsRect = { x: rect.x + dp(14), y: listRect.y + listRect.height + dp(8), width: rect.width - dp(28), height: buttonH };
    const halves = ui.columns(buttonsRect, [{ weight: 1 }, { weight: 1 }], dp(6));
    if (ui.button('song:new', 'New Song', halves[0], { primary: true, enabled: !sequenceRecording, icon: 'plus' }) && !interfaceLocked)
        openEditor('New song', nextSongName(), 'newSong', 0);
    const presetEnabled = selectedSongId > 0 && !sequenceRecording;
    if (ui.button('preset:new', 'New Preset', halves[1], { enabled: presetEnabled, icon: 'plus' }) && !interfaceLocked)
        openEditor('New preset', nextPresetName(), 'newPreset', 0);
    textAt(songs.length + ' songs / ' + livePresets.length + ' presets', rect.x + dp(14),
        buttonsRect.y + buttonH + dp(6), ui.sp(11), COLORS.muted);
}

function handleLibraryResult(result: any): void {
    if (result.selectedId) {
        const parts = String(result.selectedId).split(':');
        if (parts[0] === 's') selectSong(Number(parts[1]));
        if (parts[0] === 'p') selectPreset(Number(parts[1]), Number(parts[2]));
    }
    if (result.actionId) {
        const parts = String(result.actionId).split(':');
        if (parts[0] === 's') showSongMenu(Number(parts[1]));
        if (parts[0] === 'p') showPresetMenu(Number(parts[1]), Number(parts[2]));
    }
    if (result.move) {
        const from = String(result.move.fromId).split(':');
        const to = String(result.move.toId).split(':');
        if (from[0] === 's') moveSong(Number(from[1]), Number(to[1]));
        if (from[0] === 'p') movePreset(Number(from[1]), Number(from[2]), Number(to[2]));
    }
}

function drawSequences(rect: any, expansion: number): void {
    const dp = ui.dp;
    panel(rect, 'Sequences');
    let toggleIcon = 'plus';
    let toggleLabel = 'Show sequences';
    if (sequencesExpanded) {
        toggleIcon = 'minus';
        toggleLabel = 'Hide sequences';
    }
    const toggleRect = { x: rect.x + rect.width - dp(44), y: rect.y + dp(4), width: dp(36), height: dp(32) };
    if (ui.iconButton('sequence:toggle', toggleIcon, toggleRect, { label: toggleLabel }) && !interfaceLocked)
        sequencesExpanded = !sequencesExpanded;
    if (expansion < 0.01 || rect.height <= dp(52)) return;

    const content = { x: rect.x + dp(10), y: rect.y + dp(40), width: rect.width - dp(20), height: rect.height - dp(48) };
    ui.layer(content, { opacity: expansion }, () => {
        if (selectedPresetId <= 0) {
            textAt('Select a preset to manage note memos', content.x + dp(4), content.y + dp(6), ui.sp(11), COLORS.muted);
            return;
        }
        drawSequenceMemos(content);
    });
}

function drawSequenceMemos(rect: any): void {
    const dp = ui.dp;
    const recordingHere = sequenceRecording && sequenceRecordPresetId === selectedPresetId;
    let titleColor = COLORS.muted;
    if (recordingHere) titleColor = COLORS.red;
    if (sequencePlaying) titleColor = COLORS.green;
    textAt(fitText('For ' + selectedPresetName(), rect.width - dp(8), ui.sp(11)), rect.x + dp(4), rect.y, ui.sp(11), titleColor);

    const buttons = ui.columns({ x: rect.x, y: rect.y + dp(20), width: rect.width, height: dp(32) },
        [{ weight: 1 }, { weight: 1 }], dp(6));
    const recordEnabled = !sequenceRecording && selectedPresetId > 0 && inputHandle >= 0;
    if (ui.button('sequence:record', 'Rec', buttons[0], { variant: 'danger', enabled: recordEnabled, icon: 'record' }) && !interfaceLocked)
        startSequenceRecording();
    if (ui.button('sequence:stop', 'Stop', buttons[1], { enabled: sequenceRecording, icon: 'stop' }) && !interfaceLocked)
        stopSequenceRecording();

    let detail = String(presetSequences.length) + ' memos';
    let detailColor = COLORS.muted;
    if (recordingHere) {
        detail = 'Recording ' + sequenceRecordingEvents.length + ' events - ' + formatSequenceDuration(now() - sequenceRecordStartedAt);
        detailColor = COLORS.red;
    } else if (sequencePlaying) {
        detail = 'Playing ' + sequencePlaybackName;
        detailColor = COLORS.green;
    }
    textAt(fitText(detail, rect.width - dp(8), ui.sp(10)), rect.x + dp(4), rect.y + dp(58), ui.sp(10), detailColor);

    const listRect = { x: rect.x, y: rect.y + dp(76), width: rect.width, height: Math.max(0, rect.height - dp(76)) };
    if (presetSequences.length === 0) {
        textAt('No memos yet', listRect.x + dp(4), listRect.y + dp(4), ui.sp(10), COLORS.muted);
        return;
    }
    const rowH = dp(40);
    ui.scroll('memos', listRect, presetSequences.length * rowH, content => {
        for (let index = 0; index < presetSequences.length; index++) {
            const row = { x: content.x, y: content.y + index * rowH, width: content.width, height: rowH - dp(4) };
            if (row.y + row.height < listRect.y || row.y > listRect.y + listRect.height) continue;
            drawMemoRow(presetSequences[index], row);
        }
    }, { dragThreshold: TOUCH_MOVE_DP, radius: dp(5) });
}

function drawMemoRow(sequence: any, row: any): void {
    const dp = ui.dp;
    const memoId = sequenceId(sequence);
    const playing = sequencePlayingId === memoId;
    let fill = COLORS.panelSoft;
    let ink = COLORS.text;
    if (playing) {
        fill = COLORS.sequencePlaying;
        ink = COLORS.green;
    }
    ui.fill(row, fill, dp(5));
    const deleteRect = { x: row.x + row.width - dp(34), y: row.y + dp(2), width: dp(32), height: row.height - dp(4) };
    const playRect = { x: deleteRect.x - dp(36), y: deleteRect.y, width: dp(32), height: deleteRect.height };
    const textW = playRect.x - row.x - dp(14);
    textAt(fitText(sequenceMemoName(sequence), textW, ui.sp(11)), row.x + dp(10), row.y + dp(4), ui.sp(11), ink);
    const info = sequenceEventCount(sequence) + ' events / ' + formatSequenceDuration(sequenceDurationForRow(sequence));
    textAt(fitText(info, textW, ui.sp(9)), row.x + dp(10), row.y + dp(20), ui.sp(9), COLORS.muted);
    const playEnabled = !sequenceRecording && !sequencePlaying && outputHandle >= 0;
    if (ui.iconButton('memo:play:' + memoId, 'play', playRect, { label: 'Play ' + sequenceMemoName(sequence), enabled: playEnabled }) && !interfaceLocked)
        playPresetSequence(memoId);
    if (ui.iconButton('memo:delete:' + memoId, 'trash', deleteRect,
        { label: 'Delete ' + sequenceMemoName(sequence), enabled: !sequenceRecording, variant: 'danger' }) && !interfaceLocked)
        deletePresetSequence(memoId);
}

function drawLog(rect: any): void {
    const dp = ui.dp;
    ui.fill(rect, COLORS.panel, dp(7));
    const commands = capturedCommandsNewestFirst();
    let commandText = 'Nothing received';
    let commandColor = COLORS.amber;
    if (commands.length > 0) {
        commandText = commands[0].label;
        commandColor = COLORS.green;
    }
    // Edit: the captured state goes into every new preset, so it can be
    // cleared here (the X key does the same).
    let clearW = 0;
    if (!liveMode) {
        const count = capturedCount();
        clearW = dp(150);
        const clearRect = { x: rect.x + rect.width - clearW - dp(8), y: rect.y + dp(8), width: clearW, height: rect.height - dp(16) };
        const clearEnabled = count > 0 && !sequenceRecording && !interfaceLocked;
        if (ui.button('capture:clear', 'Clear capture (' + count + ')', clearRect, { variant: 'ghost', enabled: clearEnabled }) && clearEnabled)
            clearCapture();
        clearW += dp(16);
    }
    const textW = rect.width - clearW;
    const size = ui.sp(13);
    const selection = fitText(selectedSongName() + ' / ' + selectedPresetName(), textW - dp(48), size);
    const selectionW = ui.textWidth(selection, size);
    textAt(selection, rect.x + dp(14), rect.y + dp(9), size, COLORS.text);
    textAt(fitText(commandText, textW - dp(76) - selectionW, ui.sp(11)), rect.x + dp(28) + selectionW,
        rect.y + dp(11), ui.sp(11), commandColor);
    if (eventLog.length > 0) {
        const event = eventLog[0];
        let message = event.text;
        if (event.detail.length > 0) message += ' - ' + event.detail;
        let color = event.color;
        if (now() - event.time > 5000) color = COLORS.muted;
        textAt(fitText(message, textW - dp(28), ui.sp(12)), rect.x + dp(14), rect.y + dp(31), ui.sp(12), color);
    }
}

// ── Live mode ────────────────────────────────────────────────────────────

/** Place song separators and preset chips in rows that wrap to `width`. */
function liveLayout(width: number): any {
    const dp = ui.dp;
    const gap = dp(10);
    const chipH = dp(56);
    const separatorH = dp(34);
    const chipSize = ui.sp(20);
    const items: any[] = [];
    let cursorX = 0;
    let cursorY = 0;
    let previousSongId = -1;
    for (let index = 0; index < livePresets.length; index++) {
        const preset = livePresets[index];
        const songId = Number(preset.live_song_id || 0);
        if (songId !== previousSongId) {
            previousSongId = songId;
            if (cursorX > 0) {
                cursorX = 0;
                cursorY += chipH + gap;
            }
            items.push({ kind: 'song', songId: songId, name: String(preset.live_song_name || 'Song'), x: 0, y: cursorY, w: width, h: separatorH });
            cursorY += separatorH;
        }
        const name = rowName(preset);
        const chipW = clamp(ui.textWidth(name, chipSize) + dp(38), dp(132), Math.min(dp(300), width));
        if (cursorX > 0 && cursorX + chipW > width) {
            cursorX = 0;
            cursorY += chipH + gap;
        }
        items.push({ kind: 'preset', songId: songId, presetId: rowId(preset), songName: String(preset.live_song_name || 'Song'),
            name: name, x: cursorX, y: cursorY, w: chipW, h: chipH });
        cursorX += chipW + gap;
    }
    let height = cursorY;
    if (cursorX > 0) height += chipH;
    return { items: items, height: height };
}

function drawLive(rect: any): void {
    const dp = ui.dp;
    const connected = liveConnectionOk();
    const layout = liveLayout(rect.width - dp(12));
    if (livePresets.length === 0) {
        textAt('No presets configured', rect.x + dp(4), rect.y + dp(14), ui.sp(17), COLORS.muted);
        return;
    }
    ui.scroll('live', rect, layout.height, content => {
        for (let index = 0; index < layout.items.length; index++) {
            const item = layout.items[index];
            const box = { x: content.x + item.x, y: content.y + item.y, width: item.w, height: item.h };
            if (box.y + box.height < rect.y || box.y > rect.y + rect.height) continue;
            if (item.kind === 'song') drawLiveSong(item, box);
            else drawLiveChip(item, box, connected);
        }
    }, { dragThreshold: TOUCH_MOVE_DP, radius: 0 });
}

function drawLiveSong(item: any, box: any): void {
    const dp = ui.dp;
    const color = liveSongColor(item.songId);
    const size = ui.sp(15);
    textAt(item.name, box.x + dp(4), box.y + dp(7), size, color);
    const lineX = box.x + ui.textWidth(item.name, size) + dp(16);
    ui.fill({ x: lineX, y: box.y + dp(16), width: Math.max(0, box.x + box.width - lineX), height: dp(2) }, color, dp(1));
}

function drawLiveChip(item: any, box: any, connected: boolean): void {
    const dp = ui.dp;
    const color = liveSongColor(item.songId);
    const recalled = item.presetId === lastRecalledPresetId;
    const flashing = recalled && now() - lastRecallTime < RECALL_FLASH_MS;
    let cursor = null;
    if (connected) cursor = 'pointer';
    const state = ui.interact('live:' + item.songId + ':' + item.presetId, box, {
        cursor: cursor, focusable: connected,
        a11y: { role: 'button', label: item.name + ', ' + item.songName, disabled: !connected, selected: recalled }
    });
    let fill = COLORS.livePreset;
    let ink = color;
    let border = color;
    if (recalled) fill = COLORS.livePresetRecalled;
    if (state.hovered && connected) fill = COLORS.panelSoft;
    if (flashing) {
        fill = color;
        ink = COLORS.black;
    }
    if (!connected) {
        fill = COLORS.panel;
        ink = COLORS.muted;
        border = COLORS.border;
    }
    const press = ui.spring('live:press:' + item.presetId, state.held && connected ? 1 : 0);
    const inset = press * dp(2);
    const shape = { x: box.x + inset, y: box.y + inset, width: box.width - inset * 2, height: box.height - inset * 2 };
    ui.fill(shape, ui.color('live:fill:' + item.presetId, fill), dp(8));
    let borderWidth = dp(1);
    if (recalled) borderWidth = dp(2);
    ui.outline(shape, border, borderWidth, dp(8));
    const size = ui.sp(20);
    textAt(fitText(item.name, shape.width - dp(24), size), shape.x + dp(12), shape.y + (shape.height - size) / 2 - dp(2), size, ink);
    // One deliberate tap: on release, not after the finger moved.
    const moved = Math.abs(ui.input.pointer.x - pressX) > ui.dp(TOUCH_MOVE_DP) ||
        Math.abs(ui.input.pointer.y - pressY) > ui.dp(TOUCH_MOVE_DP);
    if (state.clicked && connected && !interfaceLocked && !moved) recallLivePreset(item.songId, item.presetId);
}

// ── Dialogs ──────────────────────────────────────────────────────────────

function drawEditorDialog(): void {
    const dp = ui.dp;
    if (ui.dialog('editor', editor.active, content => {
        if (editor.needsFocus) {
            ui.focusField('name', editor);
            editor.needsFocus = false;
        }
        const rows = ui.rows(content, [dp(48), dp(14), dp(44)], 0);
        ui.field('name', rows[0], editor, { label: editor.title, maxLength: 48 });
        const buttons = ui.columns(rows[2], [{ weight: 1 }, { weight: 1 }], dp(10));
        if (ui.button('cancel', 'Cancel', buttons[0])) closeEditor();
        const valid = editor.value.trim().length > 0;
        const saved = ui.button('save', 'Save', buttons[1], { primary: true, enabled: valid });
        if (valid && (saved || sys.input.isKeyPressed(SDL_ENTER))) confirmEditor();
    }, { title: editor.title, width: dp(520), height: dp(200) })) closeEditor();
}

function menuButtons(content: any, count: number): any[] {
    const sizes: any[] = [];
    for (let index = 0; index < count; index++) sizes.push(ui.dp(46));
    return ui.rows(content, sizes, ui.dp(10));
}

function drawPresetMenu(): void {
    const dp = ui.dp;
    let title = 'Preset actions';
    const preset = selectedPreset();
    if (preset) title = rowName(preset);
    if (ui.dialog('presetmenu', presetMenu.active, content => {
        const hasPreset = selectedPresetId > 0;
        const buttons = menuButtons(content, 5);
        const updateEnabled = capturedCount() > 0 && hasPreset && !sequenceRecording;
        const recallEnabled = hasPreset && outputHandle >= 0 && !sequenceRecording;
        if (ui.button('update', 'Update with capture', buttons[0], { primary: true, enabled: updateEnabled })) {
            updatePreset(selectedPresetId);
            closePresetMenu();
        }
        if (ui.button('rename', 'Rename', buttons[1], { enabled: hasPreset && !sequenceRecording })) {
            const targetId = selectedPresetId;
            closePresetMenu();
            openEditor('Rename preset', selectedPresetName(), 'renamePreset', targetId);
        }
        if (ui.button('recall', 'Recall', buttons[2], { enabled: recallEnabled })) {
            recallPreset(selectedPresetId);
            closePresetMenu();
        }
        if (ui.button('delete', 'Delete', buttons[3], { variant: 'danger', enabled: hasPreset && !sequenceRecording })) {
            deletePreset(selectedPresetId);
            closePresetMenu();
        }
        if (ui.button('cancel', 'Cancel', buttons[4], { variant: 'ghost' })) closePresetMenu();
    }, { title: title, width: dp(420), height: dp(56 + 5 * 46 + 4 * 10 + 40) })) closePresetMenu();
}

function drawSongMenu(): void {
    const dp = ui.dp;
    let title = 'Song actions';
    if (selectedSongId > 0) title = selectedSongName();
    if (ui.dialog('songmenu', songMenu.active, content => {
        const enabled = selectedSongId > 0 && !sequenceRecording;
        const buttons = menuButtons(content, 3);
        if (ui.button('rename', 'Rename', buttons[0], { enabled: enabled })) {
            const targetId = selectedSongId;
            closeSongMenu();
            openEditor('Rename song', selectedSongName(), 'renameSong', targetId);
        }
        if (ui.button('delete', 'Delete', buttons[1], { variant: 'danger', enabled: enabled })) {
            deleteSong(selectedSongId);
            closeSongMenu();
        }
        if (ui.button('cancel', 'Cancel', buttons[2], { variant: 'ghost' })) closeSongMenu();
    }, { title: title, width: dp(420), height: dp(56 + 3 * 46 + 2 * 10 + 40) })) closeSongMenu();
}

function drawImportDialog(): void {
    const dp = ui.dp;
    const visible = Math.max(1, Math.min(6, importDialog.files.length));
    const height = dp(96 + 22 + visible * 44 + 28 + 44 + 40);
    if (ui.dialog('import', importDialog.active, content => {
        textAt('Select an export from files/. Current data will be replaced.', content.x, content.y, ui.sp(11), COLORS.muted);
        const listRect = { x: content.x, y: content.y + dp(22), width: content.width, height: visible * dp(44) };
        if (importDialog.files.length === 0) {
            textAt('No midi_preset_saver_*.json exports found', listRect.x, listRect.y + dp(12), ui.sp(13), COLORS.amber);
        } else {
            ui.scroll('files', listRect, importDialog.files.length * dp(44), list => {
                for (let index = 0; index < importDialog.files.length; index++) {
                    const row = { x: list.x, y: list.y + index * dp(44), width: list.width, height: dp(40) };
                    if (ui.choice('file:' + index, importDialog.files[index].name, row, index === importDialog.selected)) {
                        importDialog.selected = index;
                        importDialog.error = '';
                    }
                }
            });
        }
        const errorY = listRect.y + listRect.height + dp(6);
        if (importDialog.error.length > 0)
            textAt(fitText(importDialog.error, content.width, ui.sp(11)), content.x, errorY, ui.sp(11), COLORS.red);
        const buttons = ui.columns({ x: content.x, y: errorY + dp(26), width: content.width, height: dp(42) },
            [{ weight: 1 }, { weight: 1 }, { weight: 1 }], dp(8));
        if (ui.button('browse', 'Browse device', buttons[0])) browseDatabaseImport();
        if (ui.button('cancel', 'Cancel', buttons[1])) closeImportDialog();
        if (ui.button('confirm', 'Import selected', buttons[2], { variant: 'danger', enabled: importDialog.files.length > 0 }))
            importDatabaseFile(importDialog.files[importDialog.selected]);
    }, { title: 'Import database', width: dp(640), height: height })) closeImportDialog();
}

// ── Frame ────────────────────────────────────────────────────────────────

function dialogOpen(): boolean {
    return editor.active || presetMenu.active || songMenu.active || importDialog.active;
}

function handleKeyboardShortcuts(): void {
    if (interfaceLocked || dialogOpen()) return;
    // Keys belong to a focused widget (Enter clicks it) or a field being edited.
    if (ui.focus !== null || ui.keyFocus !== null) return;
    if (!liveMode && !sequenceRecording && sys.input.isKeyPressed(SDL_X)) clearCapture();
    if (liveMode || !sequenceRecording) {
        if (sys.input.isKeyPressed(SDL_ENTER) && selectedPresetId > 0) recallPreset(selectedPresetId);
    }
}

function lerpRect(from: any, to: any, amount: number): any {
    return {
        x: from.x + (to.x - from.x) * amount, y: from.y + (to.y - from.y) * amount,
        width: from.width + (to.width - from.width) * amount, height: from.height + (to.height - from.height) * amount
    };
}

function frame(timestamp: number): void {
    frameClockMs = timestamp;
    frameClockSet = true;
    const input = sys.input.get();
    if (input.pointer && input.pointer.pressed) {
        pressX = input.pointer.x;
        pressY = input.pointer.y;
    }
    updateSequencePlayback();
    pumpRecall();
    ui.begin(input);
    ui.clear();
    const dp = ui.dp;
    const bounds = ui.bounds;

    // Edit and Live share the Library's place: it morphs into the Live flow
    // while the Edit-only panels fade, and input waits until it settles.
    let liveTarget = 0;
    if (liveMode) liveTarget = 1;
    const live = ui.spring('mode', liveTarget, { stiffness: 210, damping: 30 });
    interfaceLocked = Math.abs(live - liveTarget) > 0.01;

    const margin = dp(12);
    const header = { x: bounds.x + margin, y: bounds.y + dp(10), width: bounds.width - margin * 2, height: dp(32) };
    drawHeader(header, live);

    const devices = { x: header.x, y: header.y + header.height + dp(8), width: header.width, height: dp(36) };
    const logRect = { x: header.x, y: bounds.y + bounds.height - margin - dp(56), width: header.width, height: dp(56) };
    const areaTop = devices.y + devices.height + dp(10);
    const area = { x: header.x, y: areaTop, width: header.width, height: Math.max(dp(160), logRect.y - margin - areaTop) };
    const expansion = ui.spring('sequences', sequencesExpanded ? 1 : 0);
    let libraryRect = area;
    let sequenceRect = area;
    if (area.width < dp(720)) {
        const sequenceH = dp(48) + (area.height * 0.5 - dp(48)) * expansion;
        libraryRect = { x: area.x, y: area.y, width: area.width, height: area.height - sequenceH - margin };
        sequenceRect = { x: area.x, y: area.y + libraryRect.height + margin, width: area.width, height: sequenceH };
    } else {
        const sequenceW = area.width * (0.12 + 0.13 * expansion);
        libraryRect = { x: area.x, y: area.y, width: area.width - sequenceW - margin, height: area.height };
        sequenceRect = { x: area.x + libraryRect.width + margin, y: area.y, width: sequenceW, height: area.height };
    }
    const liveTop = header.y + header.height + dp(8);
    const liveRect = { x: bounds.x + dp(6), y: liveTop, width: bounds.width - dp(12), height: logRect.y - dp(10) - liveTop };

    if (live < 0.999) {
        ui.layer(bounds, { opacity: 1 - live }, () => {
            drawDevices(devices);
            drawSequences(sequenceRect, expansion);
        });
    }
    // The log panel stays put in both modes: recall feedback is always in one place.
    drawLog(logRect);
    // The shared surface: the Library panel morphs into the Live flow. Its
    // content hands over in sequence: Edit rows fade out during the first
    // half, Live chips fade in during the second, so they never overlap.
    const shared = lerpRect(libraryRect, liveRect, live);
    const editAlpha = clamp(1 - live * 2, 0, 1);
    const liveAlpha = clamp(live * 2 - 1, 0, 1);
    if (live > 0.001 && live < 0.999) panel(shared, '');
    if (editAlpha > 0.001) ui.layer(shared, { opacity: editAlpha }, () => drawLibrary(shared));
    if (liveAlpha > 0.001) ui.layer(shared, { opacity: liveAlpha }, () => drawLive(liveRect));

    drawPresetMenu();
    drawSongMenu();
    drawImportDialog();
    drawEditorDialog();
    handleKeyboardShortcuts();
    ui.end();

    applyGlassEffect(sys.window.getWidth(), sys.window.getHeight());
    // Every frame: the glass shader animates and memo playback is timed.
    sys.animation.requestFrame(frame);
}

function applyGlassEffect(width: number, height: number): void {
    sys.gl.bindScreen();
    sys.gl.setUniform2f(glassProgram, 'u_resolution', width, height);
    sys.gl.setUniform1f(glassProgram, 'u_time', now() / 1000.0);
    sys.gl.drawFullscreen(glassProgram);
}

ensureDefaultSong();
selectedSongId = Number(loadAppState('selected_song_id', '0'));
selectedPresetId = Number(loadAppState('selected_preset_id', '0'));
loadSongs();
requestKeepScreenOn();
refreshMidiDevices();
sys.midi.onDevicesChanged(handleMidiDevicesChanged);
loadUiFont();
sys.log('MIDI Preset Saver ready');
sys.animation.requestFrame(frame);
