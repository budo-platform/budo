/// <reference path="budo.d.ts" />

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

interface Button {
    id: string;
    x: number;
    y: number;
    w: number;
    h: number;
    label: string;
    disabled: boolean;
    tone: string;
    clipRect: UiRect | null;
}

interface PointerHit {
    x: number;
    y: number;
    down: boolean;
    pressed: boolean;
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

interface EditLibraryItem {
    kind: string;
    id: number;
    songId: number;
    top: number;
    height: number;
}

interface UiRect {
    x: number;
    y: number;
    w: number;
    h: number;
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

let density = 1;
try {
    density = sys.window.getDisplayDensity();
} catch (error) {
    density = 1;
}

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
let liveScroll = 0;
let liveContentHeight = 0;
let editLibraryScroll = 0;
let editLibraryContentHeight = 0;
let editLibraryItems: EditLibraryItem[] = [];
let editLibraryRegionX = 0;
let editLibraryRegionY = 0;
let editLibraryRegionW = 0;
let editLibraryRegionH = 0;
let sequenceScroll = 0;
let sequencePanelExpansion = 0;
let sequencePanelExpansionFrom = 0;
let sequencePanelExpansionTarget = 0;
let sequencePanelAnimationStartedAt = 0;
let liveMode = false;
let modeTransitionActive = false;
let modeTransitionToLive = false;
let modeTransitionProgress = 0;
let modeTransitionStartedAt = 0;
let songRegionX = 0;
let songRegionY = 0;
let songRegionW = 0;
let songRegionH = 0;
let songRegionRowH = 1;
let songRegionVisible = 1;
let presetRegionX = 0;
let presetRegionY = 0;
let presetRegionW = 0;
let presetRegionH = 0;
let presetRegionRowH = 1;
let presetRegionVisible = 1;
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
let sequencePlaying = false;
let sequencePlayingId = 0;
let sequencePlaybackStartedAt = 0;
let sequencePlaybackEvents: NoteSequenceEvent[] = [];
let sequencePlaybackIndex = 0;
let sequencePlaybackSent = 0;
let sequencePlaybackName = '';
let buttons: Button[] = [];
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
    targetId: 0
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

const LONG_PRESS_MS = 480;
const TOUCH_MOVE_DP = 18;
let touchActive = false;
let touchList = '';
let touchMoved = false;
let touchStartX = 0;
let touchStartY = 0;
let touchStartScroll = 0;
let touchStartTime = 0;
let touchRowId = 0;
let touchActionId = '';

const REORDER_EDGE_DP = 30;
const REORDER_AUTOSCROLL_ROWS = 0.3;
const SEQUENCE_PANEL_ANIMATION_MS = 250;
const MODE_TRANSITION_MS = 400;
const SPACE_XS_DP = 4;
const SPACE_SM_DP = 6;
const SPACE_MD_DP = 8;
const SPACE_LG_DP = 12;
const SPACE_XL_DP = 18;
const COMPACT_DRAG_HANDLE_DP = 24;
const COMPACT_ROW_WIDTH_DP = 190;
const DEVICE_SELECTOR_TOP_DP = 54 - 5;
let reorderActive = false;
let reorderList = '';
let reorderId = 0;
let reorderGrabDY = 0;
let reorderSongId = 0;
let reorderItems: any[] = [];

let canvasClipRects: UiRect[] = [];

function resetClipRects() {
    canvasClipRects = [];
}

function getCurrentClipRect(): UiRect | null {
    if (canvasClipRects.length === 0) return null;
    return canvasClipRects[canvasClipRects.length - 1];
}

function clipCanvas(x: number, y: number, w: number, h: number) {
    sys.canvas.save();
    sys.canvas.clipRect(x, y, w, h);
    canvasClipRects.push({ x, y, w, h });
}

function unclipCanvas() {
    sys.canvas.restore();
    canvasClipRects.pop();
}

function dp(value: number): number {
    return Math.round(value * density);
}

function now(): number {
    return Date.now();
}

function smootherStep(value: number): number {
    const t = clamp(value, 0, 1);
    return t * t * t * (t * (t * 6 - 15) + 10);
}

function updateSequencePanelAnimation(): void {
    if (sequencePanelExpansion === sequencePanelExpansionTarget) return;
    const elapsed = now() - sequencePanelAnimationStartedAt;
    const progress = clamp(elapsed / SEQUENCE_PANEL_ANIMATION_MS, 0, 1);
    const eased = smootherStep(progress);
    sequencePanelExpansion = sequencePanelExpansionFrom +
        (sequencePanelExpansionTarget - sequencePanelExpansionFrom) * eased;
    if (progress >= 1) sequencePanelExpansion = sequencePanelExpansionTarget;
}

function toggleSequencePanel(): void {
    sequencePanelExpansionFrom = sequencePanelExpansion;
    sequencePanelExpansionTarget = sequencePanelExpansionTarget > 0.5 ? 0 : 1;
    sequencePanelAnimationStartedAt = now();
}

function editLibraryRect(width: number, height: number): UiRect {
    const margin = dp(12);
    const topY = dp(92);
    const statusH = dp(82);
    const panelH = Math.max(dp(220), height - topY - statusH - margin);
    if (width < dp(720)) {
        const utilityTotalH = clamp(Math.floor(panelH * 0.40), dp(240), dp(300));
        const expandedSequenceH = Math.floor(utilityTotalH * 0.56);
        const collapsedSequenceH = dp(48);
        const sequenceH = Math.floor(collapsedSequenceH +
            (expandedSequenceH - collapsedSequenceH) * sequencePanelExpansion);
        const libraryH = Math.max(dp(220), panelH - utilityTotalH - margin * 2);
        return {
            x: margin, y: topY, w: width - margin * 2,
            h: libraryH + expandedSequenceH - sequenceH
        };
    }
    const available = width - margin * 4;
    const captureW = Math.floor(available * 0.23);
    const expandedSequenceW = Math.floor(available * 0.25);
    const collapsedSequenceW = Math.floor(available * 0.12);
    const sequenceW = Math.floor(collapsedSequenceW +
        (expandedSequenceW - collapsedSequenceW) * sequencePanelExpansion);
    return { x: margin, y: topY, w: available - captureW - sequenceW, h: panelH };
}

function interpolateRect(from: UiRect, to: UiRect, amount: number): UiRect {
    return {
        x: from.x + (to.x - from.x) * amount,
        y: from.y + (to.y - from.y) * amount,
        w: from.w + (to.w - from.w) * amount,
        h: from.h + (to.h - from.h) * amount
    };
}

function startModeTransition(toLive: boolean): void {
    modeTransitionActive = true;
    modeTransitionToLive = toLive;
    modeTransitionProgress = 0;
    modeTransitionStartedAt = now();
    touchActive = false;
}

function updateModeTransition(): void {
    if (!modeTransitionActive) return;
    const elapsed = now() - modeTransitionStartedAt;
    modeTransitionProgress = clamp(elapsed / MODE_TRANSITION_MS, 0, 1);
    if (modeTransitionProgress >= 1) {
        modeTransitionActive = false;
        liveMode = modeTransitionToLive;
        modeTransitionProgress = 0;
    }
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

function commandFromMidiMessage(message: MidiMessage): MidiCommand | null {
    const receivedAt = now();
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

    if (type === sys.midi.CHANNEL_PRESSURE) {
        return {
            key: 'channel-pressure:' + channel,
            label: 'Pressure ch ' + channelLabel(channel) + ' = ' + data1,
            bytes: [status, data1],
            receivedAt: receivedAt
        };
    }

    if (type === sys.midi.POLY_PRESSURE) {
        return {
            key: 'poly-pressure:' + channel + ':' + data1,
            label: 'Poly ch ' + channelLabel(channel) + ' note ' + data1 + ' = ' + data2,
            bytes: [status, data1, data2],
            receivedAt: receivedAt
        };
    }

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

function sequenceEventFromMidiMessage(message: MidiMessage): NoteSequenceEvent | null {
    const type = midiMessageType(message);
    if (type !== sys.midi.NOTE_ON && type !== sys.midi.NOTE_OFF) return null;

    const channel = midiMessageChannel(message);
    let status = byteValue(message.status);
    if (status === 0) {
        status = type + clamp(channel, 0, 15);
    }
    const note = byteValue(message.data1);
    const velocity = byteValue(message.data2);
    let label = 'Note On';
    if (type === sys.midi.NOTE_OFF || velocity === 0) {
        label = 'Note Off';
    }

    let eventTimeMs = Math.max(0, now() - sequenceRecordStartedAt);
    const timestampUs = Number(message.timestamp || 0);
    if (timestampUs > 0) {
        if (sequenceRecordMidiStartedAtUs <= 0) {
            sequenceRecordMidiStartedAtUs = timestampUs;
        }
        eventTimeMs = Math.max(0, Math.floor((timestampUs - sequenceRecordMidiStartedAtUs) / 1000));
    }

    return {
        timeMs: eventTimeMs,
        label: label + ' ch ' + channelLabel(channel) + ' ' + noteName(note) + ' v ' + velocity,
        bytes: [status, note, velocity]
    };
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

function prepareSequencePlaybackEvents(events: NoteSequenceEvent[]): NoteSequenceEvent[] {
    const prepared = normalizedSequenceEvents(events);
    const activeNotes: Record<string, number> = {};

    for (let index = 0; index < prepared.length; index++) {
        const event = prepared[index];
        const key = noteEventKey(event);
        if (isSequenceNoteOn(event)) {
            activeNotes[key] = event.timeMs;
        } else if (isSequenceNoteOff(event) && activeNotes[key] !== undefined) {
            const noteOnTime = activeNotes[key];
            if (event.timeMs - noteOnTime < MIN_PLAYBACK_NOTE_MS) {
                event.timeMs = noteOnTime + MIN_PLAYBACK_NOTE_MS;
            }
            delete activeNotes[key];
        }
    }

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

function stopSequencePlayback(): void {
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

    savePresetSequence(presetId, sequenceRecordingEvents);
    sequenceRecordingEvents = [];
}

function recordSequenceMidiMessage(message: MidiMessage): boolean {
    if (!sequenceRecording || liveMode) return false;
    const event = sequenceEventFromMidiMessage(message);
    if (!event) return false;

    if (sequenceRecordingEvents.length >= MAX_SEQUENCE_EVENTS) {
        addLog('Sequence event limit reached; hit Stop', '', COLORS.amber);
        return true;
    }

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
    sequencePlaybackStartedAt = now();
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

    const elapsed = now() - sequencePlaybackStartedAt;
    while (sequencePlaybackIndex < sequencePlaybackEvents.length) {
        const event = sequencePlaybackEvents[sequencePlaybackIndex];
        if (event.timeMs > elapsed) break;
        if (sys.midi.sendRaw(outputHandle, event.bytes)) {
            sequencePlaybackSent += 1;
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

function recallPreset(id: number): void {
    if (outputHandle < 0) {
        addLog('No MIDI output open', '', COLORS.red);
        lastRecallTime = now();
        return;
    }

    let preset = null;
    for (let index = 0; index < presets.length; index++) {
        if (rowId(presets[index]) === id) preset = presets[index];
    }
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

    let sent = 0;
    for (let index = 0; index < commands.length; index++) {
        const command = commands[index];
        const ok = sys.midi.sendRaw(outputHandle, command.bytes);
        if (ok) sent += 1;
    }

    selectedPresetId = id;
    lastRecalledPresetId = id;
    saveAppState('selected_preset_id', String(selectedPresetId));
    let sentColor = COLORS.amber;
    if (sent === commands.length) {
        sentColor = COLORS.green;
    }
    lastRecallTime = now();
    addLog('Recalled ' + rowName(preset), sent + ' / ' + commands.length + ' commands', sentColor);
}

function closeMidiHandles(): void {
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

    if (inputDevices.length > 0) {
        selectedInputIndex = wrapIndex(selectedInputIndex, inputDevices.length);
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

    if (outputDevices.length > 0) {
        selectedOutputIndex = wrapIndex(selectedOutputIndex, outputDevices.length);
        outputHandle = sys.midi.openOutput(selectedOutputIndex);
    }

    let inputName = 'none';
    if (inputDevices.length > 0) {
        inputName = inputDevices[selectedInputIndex].name;
    }
    let outputName = 'none';
    if (outputDevices.length > 0) {
        outputName = outputDevices[selectedOutputIndex].name;
    }
    let deviceColor = COLORS.amber;
    if (outputHandle >= 0) {
        deviceColor = COLORS.green;
    }
    let deviceStatus = 'Ready - input: ' + inputName;
    if (inputDevices.length === 0 && outputDevices.length === 0) {
        deviceStatus = 'Connect a MIDI device - changes are detected automatically';
    } else if (outputHandle < 0) {
        deviceStatus = 'Select a MIDI output to enable Live recall';
    }
    addLog(deviceStatus, '', deviceColor);
}

function refreshMidiDevices(): void {
    if (!midiAvailable) {
        addLog('MIDI is unavailable on this platform', '', COLORS.red);
        return;
    }
    sys.midi.refreshDevices();
    inputDevices = sys.midi.getInputDevices();
    outputDevices = sys.midi.getOutputDevices();
    if (selectedInputIndex >= inputDevices.length) selectedInputIndex = Math.max(0, inputDevices.length - 1);
    if (selectedOutputIndex >= outputDevices.length) selectedOutputIndex = Math.max(0, outputDevices.length - 1);
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
    selectedInputIndex = wrapIndex(selectedInputIndex + delta, inputDevices.length);
    openMidiHandles();
}

function selectOutput(delta: number): void {
    if (outputDevices.length === 0) return;
    selectedOutputIndex = wrapIndex(selectedOutputIndex + delta, outputDevices.length);
    openMidiHandles();
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
    if (text.length === 0 || text.length > 8 * 1024 * 1024) {
        throw new Error('Export file is empty or larger than 8 MiB');
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
    sys.input.startTextInput({
        text: editor.value,
        selectionStart: editor.caret,
        selectionEnd: editor.caret,
        multiline: false,
    });
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

function backspaceEditor(): void {
    if (!editor.active) return;
    if (editor.caret <= 0) return;
    editor.value = editor.value.slice(0, editor.caret - 1) + editor.value.slice(editor.caret);
    editor.caret--;
    editor.blinkStarted = sys.input.get().totalTime;
}

function handleEditorKeyboard(): void {
    if (!editor.active) return;
    if (sys.input.isKeyPressed(SDL_BACKSPACE)) backspaceEditor();
    if (sys.input.isKeyPressed(SDL_LEFT)) {
        editor.caret = Math.max(0, editor.caret - 1);
        editor.blinkStarted = sys.input.get().totalTime;
    }
    if (sys.input.isKeyPressed(SDL_RIGHT)) {
        editor.caret = Math.min(editor.value.length, editor.caret + 1);
        editor.blinkStarted = sys.input.get().totalTime;
    }
    if (sys.input.isKeyPressed(SDL_ENTER)) confirmEditor();
    if (sys.input.isKeyPressed(SDL_ESCAPE)) closeEditor();
}

function handleEditorTextInput(input: InputState): void {
    if (!editor.active) return;

    if (input.textEdit) {
        editor.value = input.textEdit.text.slice(0, 48);
        editor.caret = clamp(input.textEdit.selectionEnd, 0, editor.value.length);
        editor.blinkStarted = input.totalTime;
    } else if (input.text && editor.value.length < 48) {
        const inserted = input.text.slice(0, 48 - editor.value.length);
        editor.value = editor.value.slice(0, editor.caret) + inserted + editor.value.slice(editor.caret);
        editor.caret += inserted.length;
        editor.blinkStarted = input.totalTime;
    }

    if (input.composition.changed) {
        editor.compositionText = input.composition.active ? input.composition.text : '';
        editor.compositionCaret = input.composition.active ? input.composition.selectionEnd : 0;
        editor.blinkStarted = input.totalTime;
    }
}

function getPointer(input: InputState): PointerHit {
    let x = 0;
    let y = 0;
    let down = false;
    let pressed = false;

    if (input.pointer) {
        x = input.pointer.x;
        y = input.pointer.y;
        down = input.pointer.down;
        pressed = input.pointer.pressed;
    }

    if (input.mouse) {
        x = input.mouse.x;
        y = input.mouse.y;
        down = input.mouse.left;
        pressed = input.mouse.leftPressed;
    }

    return { x: x, y: y, down: down, pressed: pressed };
}

function pointInButton(pointer: PointerHit, button: UiRect): boolean {
    return pointer.x >= button.x && pointer.x <= button.x + button.w && pointer.y >= button.y && pointer.y <= button.y + button.h;
}

function hitButton(pointer: PointerHit): Button | null {
    for (let index = buttons.length - 1; index >= 0; index--) {
        const button = buttons[index];
        if (!button.disabled && pointInButton(pointer, button) && (button.clipRect === null || pointInButton(pointer, button.clipRect))) return button;
    }
    return null;
}

function registerButton(id: string, x: number, y: number, w: number, h: number, label: string, disabled: boolean, tone: string): Button {
    const currentClipRect = getCurrentClipRect();
    const button = { id: id, x: x, y: y, w: w, h: h, label: label, disabled: disabled, tone: tone, clipRect: currentClipRect };
    buttons.push(button);
    return button;
}

function buttonFill(button: Button, hovered: boolean): string {
    if (button.disabled) return COLORS.panelSoft;
    if (button.tone === 'primary') {
        if (hovered) return COLORS.primaryHover;
        return COLORS.teal;
    }
    if (button.tone === 'send') {
        if (hovered) return COLORS.sendHover;
        return COLORS.green;
    }
    if (button.tone === 'warn') {
        if (hovered) return COLORS.warnHover;
        return COLORS.amber;
    }
    if (button.tone === 'danger') {
        if (hovered) return COLORS.dangerHover;
        return COLORS.red;
    }
    if (button.tone === 'blue') {
        if (hovered) return COLORS.blueHover;
        return COLORS.blue;
    }
    if (hovered) return COLORS.panelStrong;
    return COLORS.panelSoft;
}

function drawRound(x: number, y: number, w: number, h: number, radius: number, color: string): void {
    sys.canvas.setFillColor(color);
    sys.canvas.drawRoundRect(x, y, w, h, radius, radius);
}

/** Horizontal space to reserve on the right of a list for its scrollbar. */
function scrollbarSpace(totalRows: number, visibleRows: number): number {
    if (totalRows > visibleRows) return dp(10);
    return 0;
}

/** Draw a vertical scrollbar at the right edge of a list region when it overflows. */
function drawScrollbar(regionX: number, regionY: number, regionW: number, regionH: number, scroll: number, totalRows: number, visibleRows: number): void {
    if (totalRows <= visibleRows) return;
    const trackW = dp(3);
    const trackX = regionX + regionW - trackW;
    drawRound(trackX, regionY, trackW, regionH, dp(2), COLORS.border);

    const maxScroll = Math.max(1, totalRows - visibleRows);
    const thumbH = Math.max(dp(24), regionH * (visibleRows / totalRows));
    const t = clamp(scroll / maxScroll, 0, 1);
    const thumbY = regionY + (regionH - thumbH) * t;
    drawRound(trackX, thumbY, trackW, thumbH, dp(2), COLORS.faint);
}

/** Width reserved on the right of a row for its reorder drag handle. */
function dragHandleWidth(totalRows: number): number {
    if (totalRows < 2) return 0;
    return dp(30);
}

/** Draw a grip glyph (three horizontal bars) centered at cx, cy. */
function drawDragHandle(cx: number, cy: number, color: string): void {
    const barW = dp(13);
    const barH = dp(2);
    const gap = dp(SPACE_XS_DP);
    for (let i = -1; i <= 1; i++) {
        drawRound(cx - barW / 2, cy + i * (barH + gap) - barH / 2, barW, barH, dp(1), color);
    }
}

function ellipsize(text: string, maxWidth: number, fontSize: number): string {
    if (sys.canvas.measureText(text, fontSize) <= maxWidth) return text;
    let result = text;
    while (result.length > 0 && sys.canvas.measureText(result + '...', fontSize) > maxWidth) {
        result = result.slice(0, result.length - 1);
    }
    if (result.length > 0) return result + '...';
    return '';
}

function drawText(text: string, x: number, y: number, fontSize: number, color: string): void {
    sys.canvas.setFillColor(color);
    sys.canvas.drawText(text, x, y, fontSize);
}

function drawCenteredText(text: string, x: number, y: number, w: number, h: number, fontSize: number, color: string): void {
    const label = ellipsize(text, w - dp(12), fontSize);
    const textWidth = sys.canvas.measureText(label, fontSize);
    drawText(label, x + Math.floor((w - textWidth) / 2), y + Math.floor(h / 2) + Math.floor(fontSize / 3), fontSize, color);
}

function drawButton(id: string, x: number, y: number, w: number, h: number, label: string, disabled: boolean, tone: string, pointer: PointerHit, textColorOverride: string = ""): void {
    const button = registerButton(id, x, y, w, h, label, disabled, tone);
    const hovered = !disabled && pointInButton(pointer, button);
    drawRound(x, y, w, h, dp(6), buttonFill(button, hovered));
    let textColor = COLORS.black;
    if (disabled) {
        textColor = COLORS.disabled;
    } else if (textColorOverride) {
        textColor = textColorOverride;
    }
    drawCenteredText(label, x, y, w, h, dp(12), textColor);
}

function drawPanel(x: number, y: number, w: number, h: number, title: string): void {
    drawRound(x, y, w, h, dp(7), COLORS.panel);
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(dp(1));
    sys.canvas.drawRoundRect(x, y, w, h, dp(7), dp(7));
    drawText(title, x + dp(14), y + dp(26), dp(15), COLORS.text);
}

function drawRowHit(id: string, x: number, y: number, w: number, h: number, disabled: boolean): void {
    registerButton(id, x, y, w, h, '', disabled, 'row');
}

function drawPresetSequencePanel(x: number, y: number, w: number, h: number, pointer: PointerHit): void {
    const innerX = x + dp(8);
    const innerY = y;
    const innerW = w - dp(16);
    const innerH = h;
    drawRound(innerX, innerY, innerW, innerH, dp(6), COLORS.panelSoft);

    let titleColor = COLORS.muted;
    if (sequenceRecording && sequenceRecordPresetId === selectedPresetId) titleColor = COLORS.red;
    if (sequencePlaying) titleColor = COLORS.green;
    drawText(ellipsize('For ' + selectedPresetName(), innerW - dp(16), dp(11)),
        innerX + dp(8), innerY + dp(15), dp(11), titleColor);

    const gap = dp(SPACE_SM_DP);
    const buttonY = innerY + dp(20);
    const buttonH = dp(24);
    const buttonW = Math.floor((innerW - dp(16) - gap) / 2);
    let buttonX = innerX + dp(8);
    drawButton('sequence:record', buttonX, buttonY, buttonW, buttonH, 'Rec', sequenceRecording || selectedPresetId <= 0 || inputHandle < 0, 'danger', pointer);
    buttonX += buttonW + gap;
    drawButton('sequence:stop', buttonX, buttonY, buttonW, buttonH, 'Stop', !sequenceRecording, 'warn', pointer);

    let detail = String(presetSequences.length) + ' memos';
    let detailColor = COLORS.muted;
    if (sequenceRecording && sequenceRecordPresetId === selectedPresetId) {
        detail = 'Recording ' + sequenceRecordingEvents.length + ' events - ' + formatSequenceDuration(now() - sequenceRecordStartedAt);
        detailColor = COLORS.red;
    } else if (sequencePlaying) {
        detail = 'Playing memo';
        detailColor = COLORS.green;
    }
    drawText(ellipsize(detail, innerW - dp(16), dp(10)), innerX + dp(8), buttonY + buttonH + dp(14), dp(10), detailColor);

    const listY = buttonY + buttonH + dp(20);
    const navH = dp(22);
    const rowH = dp(28);
    const rowAreaH = Math.max(dp(24), innerY + innerH - listY - navH - dp(4));
    const visibleRows = Math.max(1, Math.floor(rowAreaH / rowH));
    sequenceScroll = clamp(sequenceScroll, 0, Math.max(0, presetSequences.length - visibleRows));

    if (presetSequences.length === 0) {
        drawText('No memos yet', innerX + dp(8), listY + dp(14), dp(10), COLORS.muted);
    }

    for (let index = 0; index < visibleRows; index++) {
        const sequenceIndex = sequenceScroll + index;
        if (sequenceIndex >= presetSequences.length) break;
        const sequence = presetSequences[sequenceIndex];
        const sequenceRowId = sequenceId(sequence);
        const rowY = listY + index * rowH;
        let rowColor = COLORS.panel;
        let rowTextColor = COLORS.text;
        if (sequencePlayingId === sequenceRowId) {
            rowColor = COLORS.sequencePlaying;
            rowTextColor = COLORS.green;
        }
        drawRound(innerX + dp(6), rowY, innerW - dp(12), rowH - dp(4), dp(5), rowColor);

        const deleteW = dp(28);
        const playW = dp(38);
        const actionY = rowY + dp(SPACE_XS_DP);
        const actionH = rowH - dp(12);
        const deleteX = innerX + innerW - dp(8) - deleteW;
        const playX = deleteX - gap - playW;
        const textW = playX - innerX - dp(14);
        drawText(ellipsize(sequenceMemoName(sequence), textW, dp(10)), innerX + dp(12), rowY + dp(12), dp(10), rowTextColor);
        const rowDetail = sequenceEventCount(sequence) + ' events / ' + formatSequenceDuration(sequenceDurationForRow(sequence));
        drawText(ellipsize(rowDetail, textW, dp(9)), innerX + dp(12), rowY + dp(23), dp(9), COLORS.muted);
        drawButton('sequence:play:' + sequenceRowId, playX, actionY, playW, actionH, 'Play', sequenceRecording || sequencePlaying || outputHandle < 0, 'send', pointer);
        drawButton('sequence:delete:' + sequenceRowId, deleteX, actionY, deleteW, actionH, 'X', sequenceRecording, 'danger', pointer);
    }

    const navY = innerY + innerH - navH;
    drawButton('sequence:up', innerX + dp(8), navY, dp(38), dp(20), 'Up', sequenceScroll <= 0, 'secondary', pointer);
    drawButton('sequence:down', innerX + innerW - dp(46), navY, dp(38), dp(20), 'Down', sequenceScroll >= Math.max(0, presetSequences.length - visibleRows), 'secondary', pointer);
    drawText(String(presetSequences.length) + ' saved', innerX + dp(54), navY + dp(15), dp(9), COLORS.muted);
}

function drawDeviceSelectors(pointer: PointerHit): void {
    const deviceSelectorRect = hasRect('device-selector');
    if (!deviceSelectorRect)
        return;

    const labelW = dp(54);
    const arrowW = dp(30);
    const gap = dp(SPACE_MD_DP);

    const deviceW = Math.floor((deviceSelectorRect.w - gap) / 2);

    drawDeviceSelector(
        'input',
        deviceSelectorRect.x,
        deviceSelectorRect.y,
        deviceW,
        deviceSelectorRect.h,
        'Input',
        deviceName(inputDevices, selectedInputIndex, 'No input'),
        inputDevices.length <= 1 || sequenceRecording,
        labelW,
        arrowW,
        pointer
    );

    drawDeviceSelector(
        'output',
        deviceSelectorRect.x + deviceW + gap,
        deviceSelectorRect.y,
        deviceW,
        deviceSelectorRect.h,
        'Output',
        deviceName(outputDevices, selectedOutputIndex, 'No output'),
        outputDevices.length <= 1 || sequenceRecording,
        labelW,
        arrowW,
        pointer
    );
}

function drawDeviceSelector(prefix: string, x: number, y: number, w: number, h: number, label: string, value: string, arrowsDisabled: boolean, labelW: number, arrowW: number, pointer: PointerHit): void {
    drawRound(x, y, w, h, dp(6), COLORS.panel);
    drawText(label, x + dp(8), y + dp(20), dp(11), COLORS.muted);
    drawButton(prefix + ':prev', x + labelW, y + dp(3), arrowW, h - dp(6), '<', arrowsDisabled, 'secondary', pointer);
    drawButton(prefix + ':next', x + w - arrowW - dp(3), y + dp(3), arrowW, h - dp(6), '>', arrowsDisabled, 'secondary', pointer);
    drawText(ellipsize(value, w - labelW - arrowW * 2 - dp(18), dp(11)), x + labelW + arrowW + dp(7), y + dp(20), dp(11), COLORS.text);
}

const LIVE_SONG_COLORS = [
    COLORS.teal,
    COLORS.blue,
    COLORS.amber,
    COLORS.songAccentRose,
    COLORS.green,
    COLORS.songAccentCopper
];

function liveSongColor(songId: number): string {
    return LIVE_SONG_COLORS[Math.abs(songId) % LIVE_SONG_COLORS.length];
}

function drawLivePresetFlow(x: number, y: number, w: number, h: number, pointer: PointerHit): void {
    const connected = liveConnectionOk();
    const flowY = y;
    const flowH = h;
    const gap = dp(10);
    const chipH = dp(54);
    const separatorH = dp(34);
    const innerW = w - dp(12);

    presetRegionX = x;
    presetRegionY = flowY;
    presetRegionW = w;
    presetRegionH = flowH;
    presetRegionRowH = 1;
    presetRegionVisible = flowH;

    let cursorX = 0;
    let cursorY = 0;
    let previousSongId = -1;
    clipCanvas(x, flowY, w, flowH);

    for (let index = 0; index < livePresets.length; index++) {
        const preset = livePresets[index];
        const songId = Number(preset.live_song_id || 0);
        if (songId !== previousSongId) {
            previousSongId = songId;
            if (cursorX > 0) {
                cursorX = 0;
                cursorY += chipH + gap;
            }
            const separatorY = flowY + cursorY - liveScroll;
            const color = liveSongColor(songId);
            drawText(String(preset.live_song_name || 'Song'), x + dp(4), separatorY + dp(22), dp(15), color);
            const separatorWidth = sys.canvas.measureText(String(preset.live_song_name || 'Song'), dp(15));
            sys.canvas.setStrokeColor(color);
            sys.canvas.setStrokeWidth(dp(2));
            sys.canvas.drawLine(x + separatorWidth + dp(16), separatorY + dp(17), x + innerW, separatorY + dp(17));
            cursorY += separatorH;
        }

        const name = rowName(preset);
        const naturalW = sys.canvas.measureText(name, dp(20)) + dp(38);
        const chipW = clamp(naturalW, dp(132), Math.min(dp(300), innerW));
        if (cursorX > 0 && cursorX + chipW > innerW) {
            cursorX = 0;
            cursorY += chipH + gap;
        }
        const chipX = x + cursorX;
        const chipY = flowY + cursorY - liveScroll;
        const presetId = rowId(preset);
        const recalled = presetId === lastRecalledPresetId;
        const color = liveSongColor(songId);
        let fill = COLORS.livePreset;
        let textColor = color;
        let borderColor = color;
        if (recalled) fill = COLORS.livePresetRecalled;
        if (recalled && now() - lastRecallTime < 700) {
            fill = color;
            textColor = COLORS.black;
        }
        if (!connected) {
            fill = COLORS.panel;
            textColor = COLORS.muted;
            borderColor = COLORS.border;
        }
        drawRound(chipX, chipY, chipW, chipH, dp(8), fill);
        sys.canvas.setStrokeColor(borderColor);
        let borderWidth = dp(1);
        if (recalled) borderWidth = dp(2);
        sys.canvas.setStrokeWidth(borderWidth);
        sys.canvas.drawRoundRect(chipX, chipY, chipW, chipH, dp(8), dp(8));
        drawText(ellipsize(name, chipW - dp(24), dp(20)), chipX + dp(12), chipY + dp(34), dp(20), textColor);
        registerButton('live:preset:' + songId + ':' + presetId,
            chipX, chipY, chipW, chipH, '', !connected, 'row');
        cursorX += chipW + gap;
    }
    liveContentHeight = cursorY + (cursorX > 0 ? chipH : 0);
    liveScroll = clamp(liveScroll, 0, Math.max(0, liveContentHeight - flowH));

    unclipCanvas();

    if (livePresets.length === 0) {
        drawText('No presets configured', x + dp(4), flowY + dp(34), dp(17), COLORS.muted);
    }
    if (liveContentHeight > flowH) {
        drawScrollbar(x, flowY, w, flowH, liveScroll, liveContentHeight, flowH);
    }
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

function drawEditLibrary(x: number, y: number, w: number, h: number, pointer: PointerHit): void {
    drawPanel(x, y, w, h, 'Presets');
    const buttonH = dp(32);

    const footerH = dp(32);
    const listY = y + dp(38);
    const listH = Math.max(dp(80), h - (listY - y) - footerH - buttonH - dp(SPACE_SM_DP));
    const songH = dp(50);
    const presetH = dp(48);
    const groupGap = dp(10);
    const childIndent = dp(22);
    const innerX = x + dp(10);
    const innerW = w - dp(20);

    const buttonY = listY + listH + dp(SPACE_SM_DP);
    const gap = dp(SPACE_SM_DP);
    const buttonW = Math.floor((w - dp(28) - gap) / 2);
    drawButton('song:new', x + dp(14), buttonY, buttonW, buttonH,
        'New Song', sequenceRecording, 'primary', pointer);
    const saveDisabled = selectedSongId <= 0 || sequenceRecording; // || capturedCount() === 0;
    drawButton('preset:new', x + dp(14) + buttonW + gap, buttonY, buttonW, buttonH,
        'New Preset', saveDisabled, 'secondary', pointer, COLORS.teal);

    editLibraryRegionX = innerX;
    editLibraryRegionY = listY;
    editLibraryRegionW = innerW;
    editLibraryRegionH = listH;
    editLibraryItems = [];

    let cursor = 0;
    for (let songIndex = 0; songIndex < songs.length; songIndex++) {
        const song = songs[songIndex];
        const songId = rowId(song);
        const children = editPresetsForSong(songId);
        editLibraryItems.push({ kind: 'song', id: songId, songId: songId, top: cursor, height: songH });
        cursor += songH;
        if (children.length === 0) cursor += dp(30);
        for (let presetIndex = 0; presetIndex < children.length; presetIndex++) {
            const presetId = rowId(children[presetIndex]);
            editLibraryItems.push({ kind: 'preset', id: presetId, songId: songId, top: cursor, height: presetH });
            cursor += presetH;
        }
        cursor += groupGap;
    }
    editLibraryContentHeight = cursor;
    editLibraryScroll = clamp(editLibraryScroll, 0, Math.max(0, editLibraryContentHeight - listH));

    clipCanvas(innerX, listY, innerW, listH);

    for (let itemIndex = 0; itemIndex < editLibraryItems.length; itemIndex++) {
        const item = editLibraryItems[itemIndex];
        const rowY = listY + item.top - editLibraryScroll;
        if (rowY + item.height < listY || rowY > listY + listH) continue;

        if (item.kind === 'song') {
            let song = null;
            for (let index = 0; index < songs.length; index++) {
                if (rowId(songs[index]) === item.id) song = songs[index];
            }
            if (!song) continue;
            const selected = item.id === selectedSongId;
            const rowW = innerW - scrollbarSpace(editLibraryContentHeight, listH);
            let handleW = 0;
            if (songs.length > 1) handleW = dp(34);
            const actionW = dp(38);
            let fill = COLORS.panelStrong;
            let textColor = COLORS.text;
            if (selected) {
                fill = COLORS.selectedSong;
                textColor = COLORS.teal;
            }
            drawRound(innerX, rowY, rowW, songH - dp(5), dp(6), fill);
            drawText(ellipsize(rowName(song), rowW - handleW - actionW - dp(24), dp(15)),
                innerX + dp(14), rowY + dp(30), dp(15), textColor);
            drawRowHit('edit:song:select:' + item.id, innerX, rowY,
                rowW - handleW - actionW, songH - dp(5), false);
            const actionX = innerX + rowW - handleW - actionW;
            drawButton('song:menu:' + item.id, actionX, rowY + dp(4), actionW,
                songH - dp(13), '•••', false, 'secondary', pointer, COLORS.muted);
            if (handleW > 0) {
                const handleX = innerX + rowW - handleW;
                drawDragHandle(handleX + handleW / 2, rowY + (songH - dp(5)) / 2, COLORS.muted);
                registerButton('edit:song:drag:' + item.id, handleX, rowY,
                    handleW, songH - dp(5), '', false, 'row');
            }
        } else {
            let preset = null;
            for (let index = 0; index < livePresets.length; index++) {
                if (rowId(livePresets[index]) === item.id) preset = livePresets[index];
            }
            if (!preset) continue;
            const rowX = innerX + childIndent;
            const rowW = innerW - childIndent - scrollbarSpace(editLibraryContentHeight, listH);
            const siblings = editPresetsForSong(item.songId);
            let handleW = 0;
            if (siblings.length > 1) handleW = dp(32);
            const actionW = dp(38);
            const selected = item.id === selectedPresetId && item.songId === selectedSongId;
            let fill = COLORS.panelSoft;
            let textColor = COLORS.text;
            if (selected) {
                fill = COLORS.selectedRow;
                textColor = COLORS.teal;
            }
            drawRound(rowX, rowY, rowW, presetH - dp(5), dp(6), fill);
            drawText(ellipsize(rowName(preset), rowW - handleW - actionW - dp(24), dp(13)),
                rowX + dp(14), rowY + dp(20), dp(13), textColor);
            drawText(commandCountForPreset(preset) + ' commands', rowX + dp(14),
                rowY + dp(38), dp(11), COLORS.muted);
            drawRowHit('edit:preset:select:' + item.songId + ':' + item.id,
                rowX, rowY, rowW - handleW - actionW, presetH - dp(5), false);
            const actionX = rowX + rowW - handleW - actionW;
            drawButton('edit:preset:menu:' + item.songId + ':' + item.id,
                actionX, rowY + dp(4), actionW, presetH - dp(13), '•••',
                false, 'secondary', pointer, COLORS.muted);
            if (handleW > 0) {
                const handleX = rowX + rowW - handleW;
                drawDragHandle(handleX + handleW / 2, rowY + (presetH - dp(5)) / 2, COLORS.faint);
                registerButton('edit:preset:drag:' + item.songId + ':' + item.id,
                    handleX, rowY, handleW, presetH - dp(5), '', false, 'row');
            }
        }
    }

    for (let songIndex = 0; songIndex < songs.length; songIndex++) {
        const songId = rowId(songs[songIndex]);
        if (editPresetsForSong(songId).length > 0) continue;
        let top = 0;
        for (let itemIndex = 0; itemIndex < editLibraryItems.length; itemIndex++) {
            const item = editLibraryItems[itemIndex];
            if (item.kind === 'song' && item.id === songId) top = item.top + item.height;
        }
        const emptyY = listY + top - editLibraryScroll;
        if (emptyY >= listY && emptyY < listY + listH) {
            drawText('No presets', innerX + childIndent + dp(14), emptyY + dp(19), dp(11), COLORS.faint);
        }
    }

    drawScrollbar(innerX, listY, innerW, listH, editLibraryScroll, editLibraryContentHeight, listH);

    unclipCanvas();

    drawText(String(songs.length) + ' songs / ' + livePresets.length + ' presets',
        x + dp(14), y + h - dp(10), dp(11), COLORS.muted);
}

function drawSequenceZone(x: number, y: number, w: number, h: number, contentHeight: number, parentAlpha: number, pointer: PointerHit): void {
    drawRound(x, y, w, h, dp(7), COLORS.panel);
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(dp(1));
    sys.canvas.drawRoundRect(x, y, w, h, dp(7), dp(7));
    drawText(ellipsize('Sequences', w - dp(72), dp(15)),
        x + dp(14), y + dp(26), dp(15), COLORS.text);
    let toggleLabel = '-';
    if (sequencePanelExpansionTarget < 0.5) toggleLabel = '+';
    drawButton('sequence:toggle', x + w - dp(48), y + dp(8), dp(34), dp(28),
        toggleLabel, false, 'secondary', pointer, COLORS.text);
    if (h <= dp(52)) return;

    clipCanvas(x + dp(2), y + dp(38), w - dp(4), Math.max(0, h - dp(40)));
    sys.canvas.setAlpha(Math.floor(255 * parentAlpha * sequencePanelExpansion));
    if (selectedPresetId <= 0) {
        drawText('Select a preset to manage note memos', x + dp(14), y + dp(56), dp(11), COLORS.muted);
        sys.canvas.setAlpha(Math.floor(255 * parentAlpha));
        unclipCanvas();
        return;
    }
    drawPresetSequencePanel(x + dp(4), y + dp(36), w - dp(8), contentHeight - dp(44), pointer);
    sys.canvas.setAlpha(Math.floor(255 * parentAlpha));
    unclipCanvas();
}

function drawLogPanel(x: number, y: number, w: number, h: number): void {
    drawRound(x, y, w, h, dp(7), COLORS.panel);

    let commandText = 'Nothing received';
    const commands = capturedCommandsNewestFirst();
    let commandColor = COLORS.amber;
    if (commands.length) {
        const command = commands[0];
        commandColor = COLORS.green;
        commandText = command.label;
    }

    const fontSize = dp(13);
    const text = ellipsize(selectedSongName() + ' / ' + selectedPresetName(), w - dp(48), fontSize);
    const textWidth = sys.canvas.measureText(text, fontSize);
    drawText(text, x + dp(14), y + dp(23), dp(13), COLORS.text);
    drawText(ellipsize(commandText, w - dp(48) - textWidth - 2 * dp(14), dp(11)), x + 2 * dp(14) + textWidth, y + dp(23), dp(11), commandColor);

    if (eventLog.length > 0) {
        const event = eventLog[0];
        let message = event.text;
        if (event.detail.length > 0) message += ' - ' + event.detail;
        let color = event.color;
        let messageTime = event.time;

        if (now() - messageTime > 5000) color = COLORS.muted;
        drawText(ellipsize(message, w - dp(48), dp(12)),
            x + dp(14), y + dp(43), dp(12), color);
    }
}

function drawEditorOverlay(width: number, height: number, pointer: PointerHit): void {
    sys.canvas.setAlpha(210);
    drawRound(0, 0, width, height, 0, COLORS.black);
    sys.canvas.setAlpha(255);

    const dialogW = Math.min(width - dp(40), dp(720));
    const dialogH = Math.min(height - dp(40), dp(190));
    const x = Math.floor((width - dialogW) / 2);
    const y = Math.floor((height - dialogH) / 2);
    drawRound(x, y, dialogW, dialogH, dp(8), COLORS.panel);
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(dp(1));
    sys.canvas.drawRoundRect(x, y, dialogW, dialogH, dp(8), dp(8));

    drawText(editor.title, x + dp(SPACE_XL_DP), y + dp(30), dp(17), COLORS.text);
    const inputY = y + dp(48);
    const inputX = x + dp(SPACE_XL_DP);
    const inputW = dialogW - dp(SPACE_XL_DP * 2);
    const inputH = dp(48);
    const fontSize = dp(17);
    drawRound(inputX, inputY, inputW, inputH, dp(6), COLORS.panelSoft);
    sys.canvas.setStrokeColor(COLORS.blue);
    sys.canvas.setStrokeWidth(dp(1));
    sys.canvas.drawRoundRect(inputX, inputY, inputW, inputH, dp(6), dp(6));

    editor.caret = clamp(editor.caret, 0, editor.value.length);
    const beforeCaret = editor.value.slice(0, editor.caret);
    const afterCaret = editor.value.slice(editor.caret);
    const displayText = beforeCaret + editor.compositionText + afterCaret;
    const caretText = beforeCaret + editor.compositionText.slice(0, editor.compositionCaret);
    const caretAdvance = sys.canvas.measureText(caretText, fontSize);
    const textInset = dp(12);
    const textOffset = Math.max(0, caretAdvance - (inputW - textInset * 2));
    const textX = inputX + textInset - textOffset;
    const baselineY = inputY + dp(31);

    clipCanvas(inputX + dp(2), inputY + dp(2), inputW - dp(4), inputH - dp(4));
    drawText(displayText, textX, baselineY, fontSize, COLORS.text);
    if (editor.compositionText.length > 0) {
        const compositionX = textX + sys.canvas.measureText(beforeCaret, fontSize);
        const compositionW = sys.canvas.measureText(editor.compositionText, fontSize);
        sys.canvas.setStrokeColor(COLORS.teal);
        sys.canvas.setStrokeWidth(dp(1));
        sys.canvas.drawLine(compositionX, inputY + dp(37), compositionX + compositionW, inputY + dp(37));
    }
    const blinkOn = Math.floor((sys.input.get().totalTime - editor.blinkStarted) * 2) % 2 === 0;
    const caretX = textX + caretAdvance;
    if (blinkOn) {
        sys.canvas.setStrokeColor(COLORS.text);
        sys.canvas.setStrokeWidth(dp(1));
        sys.canvas.drawLine(caretX, inputY + dp(10), caretX, inputY + dp(38));
    }
    unclipCanvas();

    sys.input.updateTextInput({
        text: editor.value,
        selectionStart: editor.caret,
        selectionEnd: editor.caret,
        caret: { x: caretX, y: inputY + dp(8), width: dp(1), height: dp(32) },
    });

    const buttonY = inputY + inputH + dp(16);
    const buttonH = dp(42);
    const buttonGap = dp(10);
    const buttonW = Math.floor((inputW - buttonGap) / 2);
    drawButton('editor:cancel', inputX, buttonY, buttonW, buttonH, 'Cancel', false, 'secondary', pointer, COLORS.text);
    drawButton('editor:ok', inputX + buttonW + buttonGap, buttonY, buttonW, buttonH, 'Save', editor.value.trim().length === 0, 'primary', pointer);
}

function drawPresetMenu(width: number, height: number, pointer: PointerHit): void {
    sys.canvas.setAlpha(210);
    drawRound(0, 0, width, height, 0, COLORS.black);
    sys.canvas.setAlpha(255);

    // Full-screen backdrop hit target so tapping outside the dialog closes it.
    registerButton('presetmenu:close', 0, 0, width, height, '', false, 'row');

    const buttonH = dp(48);
    const gap = dp(10);
    const headerH = dp(58);
    const dialogW = Math.min(width - dp(40), dp(420));
    const dialogH = headerH + buttonH * 5 + gap * 4 + dp(18);
    const x = Math.floor((width - dialogW) / 2);
    const y = Math.floor((height - dialogH) / 2);
    drawRound(x, y, dialogW, dialogH, dp(8), COLORS.panel);
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(dp(1));
    sys.canvas.drawRoundRect(x, y, dialogW, dialogH, dp(8), dp(8));

    const preset = selectedPreset();
    let title = 'Preset actions';
    if (preset) title = rowName(preset);
    drawText(ellipsize(title, dialogW - dp(SPACE_XL_DP * 2), dp(17)), x + dp(SPACE_XL_DP), y + dp(36), dp(17), COLORS.text);

    const hasCapture = capturedCount() > 0;
    const hasPreset = selectedPresetId > 0;
    const updateDisabled = !hasCapture || !hasPreset || sequenceRecording;
    const renameDisabled = !hasPreset || sequenceRecording;
    const recallDisabled = !hasPreset || outputHandle < 0 || sequenceRecording;
    const deleteDisabled = !hasPreset || sequenceRecording;

    const btnX = x + dp(SPACE_XL_DP);
    const btnW = dialogW - dp(SPACE_XL_DP * 2);
    let btnY = y + headerH;
    drawButton('presetmenu:update', btnX, btnY, btnW, buttonH, 'Update with capture', updateDisabled, 'warn', pointer);
    btnY += buttonH + gap;
    drawButton('presetmenu:rename', btnX, btnY, btnW, buttonH, 'Rename', renameDisabled, 'secondary', pointer, COLORS.text);
    btnY += buttonH + gap;
    drawButton('presetmenu:recall', btnX, btnY, btnW, buttonH, 'Recall', recallDisabled, 'send', pointer);
    btnY += buttonH + gap;
    drawButton('presetmenu:delete', btnX, btnY, btnW, buttonH, 'Delete', deleteDisabled, 'danger', pointer);
    btnY += buttonH + gap;
    drawButton('presetmenu:close', btnX, btnY, btnW, buttonH, 'Cancel', false, 'secondary', pointer, COLORS.text);
}

function drawSongMenu(width: number, height: number, pointer: PointerHit): void {
    sys.canvas.setAlpha(210);
    drawRound(0, 0, width, height, 0, COLORS.black);
    sys.canvas.setAlpha(255);

    // Full-screen backdrop hit target so tapping outside the dialog closes it.
    registerButton('songmenu:close', 0, 0, width, height, '', false, 'row');

    const buttonH = dp(48);
    const gap = dp(10);
    const headerH = dp(58);
    const dialogW = Math.min(width - dp(40), dp(420));
    const dialogH = headerH + buttonH * 3 + gap * 2 + dp(18);
    const x = Math.floor((width - dialogW) / 2);
    const y = Math.floor((height - dialogH) / 2);
    drawRound(x, y, dialogW, dialogH, dp(8), COLORS.panel);
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(dp(1));
    sys.canvas.drawRoundRect(x, y, dialogW, dialogH, dp(8), dp(8));

    let title = 'Song actions';
    if (selectedSongId > 0) title = selectedSongName();
    drawText(ellipsize(title, dialogW - dp(SPACE_XL_DP * 2), dp(17)), x + dp(SPACE_XL_DP), y + dp(36), dp(17), COLORS.text);

    const hasSong = selectedSongId > 0;
    const actionDisabled = !hasSong || sequenceRecording;

    const btnX = x + dp(SPACE_XL_DP);
    const btnW = dialogW - dp(SPACE_XL_DP * 2);
    let btnY = y + headerH;
    drawButton('songmenu:rename', btnX, btnY, btnW, buttonH, 'Rename', actionDisabled, 'secondary', pointer, COLORS.text);
    btnY += buttonH + gap;
    drawButton('songmenu:delete', btnX, btnY, btnW, buttonH, 'Delete', actionDisabled, 'danger', pointer);
    btnY += buttonH + gap;
    drawButton('songmenu:close', btnX, btnY, btnW, buttonH, 'Cancel', false, 'secondary', pointer, COLORS.text);
}

function drawImportDialog(width: number, height: number, pointer: PointerHit): void {
    sys.canvas.setAlpha(210);
    drawRound(0, 0, width, height, 0, COLORS.black);
    sys.canvas.setAlpha(255);
    registerButton('import:close', 0, 0, width, height, '', false, 'row');

    const visibleCount = Math.min(6, importDialog.files.length);
    const rowH = dp(46);
    const dialogW = Math.min(width - dp(40), dp(640));
    const dialogH = dp(142) + Math.max(1, visibleCount) * rowH;
    const x = Math.floor((width - dialogW) / 2);
    const y = Math.floor((height - dialogH) / 2);
    drawRound(x, y, dialogW, dialogH, dp(8), COLORS.panel);
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(dp(1));
    sys.canvas.drawRoundRect(x, y, dialogW, dialogH, dp(8), dp(8));

    drawText('Import database', x + dp(SPACE_XL_DP), y + dp(32), dp(18), COLORS.text);
    drawText('Select an export from files/. Current data will be replaced.',
        x + dp(SPACE_XL_DP), y + dp(54), dp(11), COLORS.muted);

    const listY = y + dp(66);
    if (importDialog.files.length === 0) {
        drawText('No midi_preset_saver_*.json exports found', x + dp(SPACE_XL_DP), listY + dp(28), dp(13), COLORS.amber);
    }
    for (let index = 0; index < visibleCount; index++) {
        const entry = importDialog.files[index];
        const rowY = listY + index * rowH;
        const selected = index === importDialog.selected;
        drawRound(x + dp(SPACE_XL_DP), rowY, dialogW - dp(SPACE_XL_DP * 2), rowH - dp(5), dp(5), selected ? COLORS.panelStrong : COLORS.panelSoft);
        if (selected) {
            sys.canvas.setStrokeColor(COLORS.blue);
            sys.canvas.setStrokeWidth(dp(1));
            sys.canvas.drawRoundRect(x + dp(SPACE_XL_DP), rowY, dialogW - dp(SPACE_XL_DP * 2), rowH - dp(5), dp(5), dp(5));
        }
        drawButton('import:file:' + index, x + dp(SPACE_XL_DP), rowY, dialogW - dp(SPACE_XL_DP * 2), rowH - dp(5),
            ellipsize(entry.name, dialogW - dp(64), dp(12)), false, 'row', pointer,
            selected ? COLORS.text : COLORS.muted);
    }

    if (importDialog.error.length > 0) {
        drawText(ellipsize(importDialog.error, dialogW - dp(SPACE_XL_DP * 2), dp(11)),
            x + dp(SPACE_XL_DP), y + dialogH - dp(68), dp(11), COLORS.red);
    }
    const buttonY = y + dialogH - dp(54);
    const buttonGap = dp(8);
    const buttonW = Math.floor((dialogW - dp(SPACE_XL_DP * 2) - buttonGap * 2) / 3);
    drawButton('import:browse', x + dp(SPACE_XL_DP), buttonY, buttonW, dp(38), 'Browse device', false, 'blue', pointer);
    drawButton('import:cancel', x + dp(SPACE_XL_DP) + buttonW + buttonGap, buttonY, buttonW, dp(38), 'Cancel', false, 'secondary', pointer, COLORS.text);
    drawButton('import:confirm', x + dp(SPACE_XL_DP) + (buttonW + buttonGap) * 2, buttonY, buttonW, dp(38), 'Import selected',
        importDialog.files.length === 0, 'danger', pointer);
}

function transitionPointer(): PointerHit {
    return { x: -10000, y: -10000, down: false, pressed: false };
}

function drawFadeToBlack(x: number, y: number, w: number, h: number, amount: number): void {
    sys.canvas.setAlpha(Math.floor(255 * clamp(amount, 0, 1)));
    drawRound(x, y, w, h, 0, COLORS.black);
    sys.canvas.setAlpha(255);
}

function drawModeTransition(): void {
    const pointer = transitionPointer();
    const raw = modeTransitionProgress;
    const forward = modeTransitionToLive ? raw : 1 - raw;
    const morph = smootherStep(forward);
    const outgoingBlackAlpha = smootherStep(raw);
    const from = getRect('edit-library');
    const to = getRect('live-flow');
    const shared = interpolateRect(from, to, morph);

    drawRound(shared.x, shared.y, shared.w, shared.h, dp(7), COLORS.panel);
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(dp(1));
    sys.canvas.drawRoundRect(shared.x, shared.y, shared.w, shared.h, dp(7), dp(7));
    if (modeTransitionToLive) {
        drawFadeToBlack(shared.x, shared.y, shared.w, shared.h, outgoingBlackAlpha);
        drawLivePresetFlow(shared.x, shared.y, shared.w, shared.h, pointer);
    } else {
        drawFadeToBlack(shared.x, shared.y, shared.w, shared.h, outgoingBlackAlpha);
        drawEditLibrary(shared.x, shared.y, shared.w, shared.h, pointer);
    }
}

const rects = new Map<string, UiRect>();

function resetRects() {
    rects.clear();
}

function setRect(name: string, x: number, y: number, w: number, h: number) {
    rects.set(name, { x, y, w, h });
}

function getRect(name: string): UiRect {
    const r = rects.get(name)
    if (!r)
        throw `no rect with name '${name}'`;
    return r;
}

function hasRect(name: string): UiRect | null {
    return rects.get(name) || null;
}

function render(width: number, height: number, pointer: PointerHit): void {
    buttons = [];
    resetClipRects();
    resetRects();
    sys.canvas.clear(COLORS.bg);

    {
        const margin = dp(12);
        const top = dp(10);
        setRect('title', margin, top + dp(25), 0, 0);
    }

    {
        const margin = dp(12);
        setRect('mode-button',
            width - margin - dp(70),
            dp(18 - 6),
            dp(70),
            dp(34 - 5)
        )
    }

    if (liveMode || modeTransitionActive) {
        {
            const margin = dp(6);//dp(12);
            const flowY = dp(DEVICE_SELECTOR_TOP_DP - 4);

            setRect('live-flow',
                margin,
                flowY,
                width - margin * 2,
                Math.max(dp(160), height - dp(12) - flowY)
            )
        }
    }
    const editOrTowardsEdit = !liveMode || modeTransitionActive;
    if (editOrTowardsEdit) {
        {
            const margin = dp(12);
            const topY = dp(92 - 3);
            const statusH = dp(82);

            if (width < dp(720)) {
                const panelH = height - topY - statusH - margin;
                const available = panelH;

                const expandedSequenceH = Math.floor(available * 0.5);
                const collapsedSequenceH = Math.min(available, dp(48));
                const sequenceH = Math.floor(collapsedSequenceH + (expandedSequenceH - collapsedSequenceH) * sequencePanelExpansion);

                const libraryH = available - sequenceH;

                setRect('edit-library', margin, topY, width - margin * 2, libraryH);
                const sequenceY = topY + libraryH + margin;
                setRect('sequence-zone', margin, sequenceY, width - margin * 2, sequenceH);
            } else {
                const panelH = height - topY - statusH;
                const available = width - margin * 3;
                const expandedSequenceW = Math.floor(available * 0.25);
                const collapsedSequenceW = Math.floor(available * 0.12);
                const sequenceW = Math.floor(collapsedSequenceW +
                    (expandedSequenceW - collapsedSequenceW) * sequencePanelExpansion);
                const libraryW = available - sequenceW;

                setRect('edit-library', margin, topY, libraryW, panelH);
                setRect('sequence-zone', margin * 2 + libraryW, topY, sequenceW, panelH);
            }
        }

        {
            const margin = dp(12);
            const top = dp(10);

            const actionGap = dp(6);
            const modeButtonRect = getRect('mode-button');
            const actionW = Math.min(dp(92), Math.floor((modeButtonRect.x - dp(180) - margin - actionGap * 2) / 2));
            let actionX = modeButtonRect.x - actionGap - actionW;
            setRect('export-button', actionX, top + dp(8 - 6), actionW, dp(34 - 5))
            actionX -= actionW + actionGap;
            setRect('import-button', actionX, top + dp(8 - 6), actionW, dp(34 - 5));
        }

        {
            const margin = dp(SPACE_LG_DP);
            const panelH = dp(58);
            setRect('log-panel',
                margin,
                height - panelH - margin,
                width - margin * 2,
                panelH
            );
        }

        {
            const margin = dp(12);
            const availableW = width - margin * 2;
            const stripY = dp(DEVICE_SELECTOR_TOP_DP);
            setRect('device-selector', margin, stripY, availableW, dp(30));
        }
    }

    if (liveMode) {
        const modeButtonRect = getRect('mode-button');
        setRect('live-indicator-circle',
            modeButtonRect.x - dp(13),
            modeButtonRect.y + modeButtonRect.h / 2,
            0, 0
        )
    }

    const logPanelRect = hasRect('log-panel');
    if (logPanelRect)
        drawLogPanel(logPanelRect.x, logPanelRect.y, logPanelRect.w, logPanelRect.h);

    const titleRect = getRect('title');
    drawText('MIDI Preset Saver', titleRect.x, titleRect.y, dp(23), COLORS.text);

    const liveIndicatorRect = hasRect('live-indicator-circle');
    if (liveIndicatorRect) {
        const indicatorColor = liveConnectionOk() ? COLORS.green : COLORS.red;
        sys.canvas.setFillColor(indicatorColor);
        sys.canvas.drawCircle(liveIndicatorRect.x, liveIndicatorRect.y, dp(6));
    }

    const modeButtonRect = hasRect('mode-button');
    if (modeButtonRect) {
        const button = registerButton('mode:toggle', modeButtonRect.x, modeButtonRect.y, modeButtonRect.w, modeButtonRect.h,
            '', sequenceRecording, 'secondary');
        const hovered = !sequenceRecording && pointInButton(pointer, button);

        drawRound(modeButtonRect.x, modeButtonRect.y, modeButtonRect.w, modeButtonRect.h, dp(6), buttonFill(button, hovered));
        if (!liveMode) {
            const editLabelColor = sequenceRecording ? COLORS.disabled : COLORS.green;
            drawCenteredText('Live', modeButtonRect.x, modeButtonRect.y, modeButtonRect.w, modeButtonRect.h, dp(12), editLabelColor);
        }
        else {
            const liveLabelColor = sequenceRecording ? COLORS.disabled : COLORS.text;
            drawCenteredText('Setup', modeButtonRect.x, modeButtonRect.y, modeButtonRect.w, modeButtonRect.h, dp(12), liveLabelColor);
        }
    }

    const exportButtonRect = hasRect('export-button');
    const importButtonRect = hasRect('import-button');
    if (exportButtonRect && importButtonRect) {
        drawButton('db:export', exportButtonRect.x, exportButtonRect.y, exportButtonRect.w, exportButtonRect.h, 'Save', sequenceRecording, 'secondary', pointer, COLORS.amber);
        drawButton('db:import', importButtonRect.x, importButtonRect.y, importButtonRect.w, importButtonRect.h, 'Load', sequenceRecording, 'secondary', pointer, COLORS.text);
    }

    drawDeviceSelectors(pointer);

    const sequenceZoneRect = hasRect('sequence-zone')
    if (sequenceZoneRect) {
        drawSequenceZone(sequenceZoneRect.x, sequenceZoneRect.y, sequenceZoneRect.w, sequenceZoneRect.h, sequenceZoneRect.h, 1, pointer);
    }

    if (modeTransitionActive) {
        drawModeTransition();
    }
    else {
        const editLibraryRect = hasRect('edit-library')
        if (editLibraryRect) {
            drawEditLibrary(editLibraryRect.x, editLibraryRect.y, editLibraryRect.w, editLibraryRect.h, pointer);
        }

        const liveFlowRect = hasRect('live-flow');
        if (liveFlowRect) {
            drawLivePresetFlow(liveFlowRect.x, liveFlowRect.y, liveFlowRect.w, liveFlowRect.h, pointer);
        }
    }

    if (presetMenu.active) {
        drawPresetMenu(width, height, pointer);
    }

    if (songMenu.active) {
        drawSongMenu(width, height, pointer);
    }

    if (importDialog.active) {
        drawImportDialog(width, height, pointer);
    }

    if (editor.active) {
        drawEditorOverlay(width, height, pointer);
    }

    applyGlassEffect(width, height);
}

function applyGlassEffect(width: number, height: number): void {
    sys.gl.bindScreen();
    sys.gl.setUniform2f(glassProgram, 'u_resolution', width, height);
    sys.gl.setUniform1f(glassProgram, 'u_time', now() / 1000.0);
    sys.gl.drawFullscreen(glassProgram);
}

function pointInRect(pointer: PointerHit, x: number, y: number, w: number, h: number): boolean {
    return pointer.x >= x && pointer.x <= x + w && pointer.y >= y && pointer.y <= y + h;
}

function handleListWheel(input: InputState, pointer: PointerHit): void {
    if (modeTransitionActive) return;
    if (!input.mouse) return;
    if (input.mouse.wheelY === 0) return;

    let delta = 1;
    if (input.mouse.wheelY > 0) {
        delta = -1;
    }

    if (!liveMode && pointInRect(pointer, editLibraryRegionX, editLibraryRegionY,
        editLibraryRegionW, editLibraryRegionH)) {
        editLibraryScroll = clamp(editLibraryScroll + delta * dp(48), 0,
            Math.max(0, editLibraryContentHeight - editLibraryRegionH));
        return;
    }

    if (pointInRect(pointer, songRegionX, songRegionY, songRegionW, songRegionH)) {
        songScroll = clamp(songScroll + delta, 0, Math.max(0, songs.length - songRegionVisible));
        return;
    }

    if (pointInRect(pointer, presetRegionX, presetRegionY, presetRegionW, presetRegionH)) {
        if (liveMode) {
            liveScroll = clamp(liveScroll + delta * dp(54), 0,
                Math.max(0, liveContentHeight - presetRegionH));
            return;
        }
        presetScroll = clamp(presetScroll + delta, 0, Math.max(0, presets.length - presetRegionVisible));
    }
}

function handleNormalButton(id: string): void {
    if (id === 'sequence:toggle') {
        toggleSequencePanel();
        return;
    }
    if (id === 'mode:toggle') {
        if (sequenceRecording) {
            addLog('Hit Stop before switching modes', '', COLORS.amber);
            return;
        }
        stopSequencePlayback();
        const toLive = !liveMode;
        startModeTransition(toLive);
        return;
    }
    if (id === 'db:export') {
        exportDatabase();
        return;
    }
    if (id === 'db:import') {
        openImportDialog();
        return;
    }
    if (id.indexOf('live:preset:') === 0) {
        if (sequenceRecording) {
            addLog('Hit Stop before changing presets', '', COLORS.amber);
            return;
        }
        const parts = id.split(':');
        const songId = Number(parts[2]);
        const presetId = Number(parts[3]);
        if (songId > 0 && presetId > 0) {
            stopSequencePlayback();
            selectedSongId = songId;
            selectedPresetId = presetId;
            saveAppState('selected_song_id', String(selectedSongId));
            saveAppState('selected_preset_id', String(selectedPresetId));
            loadPresets();
            selectedPresetId = presetId;
            loadPresetSequences();
            recallPreset(presetId);
        }
        return;
    }
    if (id === 'input:prev') selectInput(-1);
    if (id === 'input:next') selectInput(1);
    if (id === 'output:prev') selectOutput(-1);
    if (id === 'output:next') selectOutput(1);
    if (id === 'song:new') openEditor('New song', nextSongName(), 'newSong', 0);
    if (id === 'song:rename') openEditor('Rename song', selectedSongName(), 'renameSong', selectedSongId);
    if (id.indexOf('edit:song:select:') === 0) {
        if (sequenceRecording) {
            addLog('Hit Stop before changing songs', '', COLORS.amber);
            return;
        }
        stopSequencePlayback();
        selectedSongId = Number(id.slice('edit:song:select:'.length));
        selectedPresetId = 0;
        saveAppState('selected_song_id', String(selectedSongId));
        loadPresets();
        return;
    }
    if (id.indexOf('edit:preset:select:') === 0) {
        if (sequenceRecording) {
            addLog('Hit Stop before changing presets', '', COLORS.amber);
            return;
        }
        const parts = id.split(':');
        const songId = Number(parts[3]);
        const presetId = Number(parts[4]);
        stopSequencePlayback();
        selectedSongId = songId;
        selectedPresetId = presetId;
        saveAppState('selected_song_id', String(selectedSongId));
        loadPresets();
        selectedPresetId = presetId;
        saveAppState('selected_preset_id', String(selectedPresetId));
        loadPresetSequences();
        return;
    }
    if (id.indexOf('edit:preset:menu:') === 0) {
        const parts = id.split(':');
        const songId = Number(parts[3]);
        const presetId = Number(parts[4]);
        selectedSongId = songId;
        selectedPresetId = presetId;
        saveAppState('selected_song_id', String(selectedSongId));
        loadPresets();
        selectedPresetId = presetId;
        saveAppState('selected_preset_id', String(selectedPresetId));
        loadPresetSequences();
        openPresetMenu(presetId);
        return;
    }
    if (id.indexOf('song:menu:') === 0) {
        const songId = Number(id.slice('song:menu:'.length));
        if (songId > 0) {
            selectedSongId = songId;
            selectedPresetId = 0;
            presetScroll = 0;
            saveAppState('selected_song_id', String(selectedSongId));
            loadPresets();
            openSongMenu(songId);
        }
        return;
    }
    if (id.indexOf('song:select:') === 0) {
        if (sequenceRecording) {
            addLog('Hit Stop before changing songs', '', COLORS.amber);
            return;
        }
        stopSequencePlayback();
        selectedSongId = Number(id.slice('song:select:'.length));
        selectedPresetId = 0;
        presetScroll = 0;
        saveAppState('selected_song_id', String(selectedSongId));
        loadPresets();
    }

    if (id === 'preset:new') openEditor('New preset', nextPresetName(), 'newPreset', 0);
    if (id.indexOf('preset:menu:') === 0) {
        const presetId = Number(id.slice('preset:menu:'.length));
        if (presetId > 0) {
            selectedPresetId = presetId;
            saveAppState('selected_preset_id', String(selectedPresetId));
            loadPresetSequences();
            openPresetMenu(presetId);
        }
        return;
    }
    if (id.indexOf('preset:select:') === 0) {
        if (sequenceRecording) {
            addLog('Hit Stop before changing presets', '', COLORS.amber);
            return;
        }
        stopSequencePlayback();
        selectedPresetId = Number(id.slice('preset:select:'.length));
        saveAppState('selected_preset_id', String(selectedPresetId));
        loadPresetSequences();
        if (liveMode) {
            recallPreset(selectedPresetId);
        }
    }

    if (id === 'capture:clear') clearCapture();
    if (id === 'sequence:record') startSequenceRecording();
    if (id === 'sequence:stop') stopSequenceRecording();
    if (id === 'sequence:up') sequenceScroll = Math.max(0, sequenceScroll - 1);
    if (id === 'sequence:down') sequenceScroll += 1;
    if (id.indexOf('sequence:play:') === 0) playPresetSequence(Number(id.slice('sequence:play:'.length)));
    if (id.indexOf('sequence:delete:') === 0) deletePresetSequence(Number(id.slice('sequence:delete:'.length)));
}

function handleEditorButton(id: string): void {
    if (id === 'editor:cancel') closeEditor();
    if (id === 'editor:ok') confirmEditor();
}

function handlePresetMenuButton(id: string): void {
    if (id === 'presetmenu:update') {
        updatePreset(selectedPresetId);
        closePresetMenu();
        return;
    }
    if (id === 'presetmenu:rename') {
        const targetId = selectedPresetId;
        closePresetMenu();
        openEditor('Rename preset', selectedPresetName(), 'renamePreset', targetId);
        return;
    }
    if (id === 'presetmenu:recall') {
        recallPreset(selectedPresetId);
        closePresetMenu();
        return;
    }
    if (id === 'presetmenu:delete') {
        deletePreset(selectedPresetId);
        closePresetMenu();
        return;
    }
    if (id === 'presetmenu:close') {
        closePresetMenu();
    }
}

function handleSongMenuButton(id: string): void {
    if (id === 'songmenu:rename') {
        const targetId = selectedSongId;
        closeSongMenu();
        openEditor('Rename song', selectedSongName(), 'renameSong', targetId);
        return;
    }
    if (id === 'songmenu:delete') {
        deleteSong(selectedSongId);
        closeSongMenu();
        return;
    }
    if (id === 'songmenu:close') {
        closeSongMenu();
    }
}

function handleImportButton(id: string): void {
    if (id.indexOf('import:file:') === 0) {
        importDialog.selected = clamp(Number(id.slice('import:file:'.length)), 0,
            Math.max(0, importDialog.files.length - 1));
        importDialog.error = '';
        return;
    }
    if (id === 'import:cancel' || id === 'import:close') {
        closeImportDialog();
        return;
    }
    if (id === 'import:browse') {
        browseDatabaseImport();
        return;
    }
    if (id === 'import:confirm' && importDialog.files.length > 0) {
        importDatabaseFile(importDialog.files[importDialog.selected]);
    }
}

function rowIdAt(pointer: PointerHit, prefix: string): number {
    for (let index = buttons.length - 1; index >= 0; index--) {
        const button = buttons[index];
        if (button.id.indexOf(prefix) === 0 && pointInButton(pointer, button)) {
            return Number(button.id.slice(prefix.length));
        }
    }
    return 0;
}

function buttonIdAt(pointer: PointerHit, prefix: string): string {
    for (let index = buttons.length - 1; index >= 0; index--) {
        const button = buttons[index];
        if (button.id.indexOf(prefix) === 0 && pointInButton(pointer, button)) return button.id;
    }
    return '';
}

function listRegionAt(pointer: PointerHit): string {
    if (liveMode) {
        if (pointInRect(pointer, presetRegionX, presetRegionY, presetRegionW, presetRegionH)) return 'preset';
        return '';
    }
    if (pointInRect(pointer, editLibraryRegionX, editLibraryRegionY,
        editLibraryRegionW, editLibraryRegionH)) return 'edit';
    if (pointInRect(pointer, songRegionX, songRegionY, songRegionW, songRegionH)) return 'song';
    if (pointInRect(pointer, presetRegionX, presetRegionY, presetRegionW, presetRegionH)) return 'preset';
    return '';
}

function applyTouchScroll(curY: number): void {
    if (touchList === 'edit') {
        editLibraryScroll = clamp(touchStartScroll + touchStartY - curY, 0,
            Math.max(0, editLibraryContentHeight - editLibraryRegionH));
    } else if (touchList === 'song') {
        const max = Math.max(0, songs.length - songRegionVisible);
        songScroll = clamp(touchStartScroll + (touchStartY - curY) / songRegionRowH, 0, max);
    } else if (touchList === 'preset') {
        if (liveMode) {
            liveScroll = clamp(touchStartScroll + touchStartY - curY, 0,
                Math.max(0, liveContentHeight - presetRegionH));
            return;
        }
        const max = Math.max(0, presets.length - presetRegionVisible);
        presetScroll = clamp(touchStartScroll + (touchStartY - curY) / presetRegionRowH, 0, max);
    }
}

function startEditReorder(kind: string, songId: number, id: number, pointer: PointerHit): void {
    let itemKind = 'preset';
    if (kind === 'edit-song') itemKind = 'song';
    let layoutItem = null;
    for (let index = 0; index < editLibraryItems.length; index++) {
        const item = editLibraryItems[index];
        if (item.id === id && item.kind === itemKind) {
            layoutItem = item;
        }
    }
    if (!layoutItem) return;
    reorderActive = true;
    reorderList = kind;
    reorderId = id;
    reorderSongId = songId;
    reorderItems = editPresetsForSong(songId);
    if (kind === 'edit-song') reorderItems = songs;
    reorderGrabDY = pointer.y - (editLibraryRegionY + layoutItem.top - editLibraryScroll);
}

function rebuildLivePresetSongOrder(songId: number): void {
    let replacement = 0;
    for (let index = 0; index < livePresets.length; index++) {
        const rowSongId = Number(livePresets[index].live_song_id || livePresets[index].song_id || 0);
        if (rowSongId === songId && replacement < reorderItems.length) {
            livePresets[index] = reorderItems[replacement++];
        }
    }
}

function updateEditReorderDrag(pointer: PointerHit): void {
    const edge = dp(REORDER_EDGE_DP);
    const maximumScroll = Math.max(0, editLibraryContentHeight - editLibraryRegionH);
    if (pointer.y < editLibraryRegionY + edge) {
        editLibraryScroll = clamp(editLibraryScroll - dp(8), 0, maximumScroll);
    } else if (pointer.y > editLibraryRegionY + editLibraryRegionH - edge) {
        editLibraryScroll = clamp(editLibraryScroll + dp(8), 0, maximumScroll);
    }

    const contentY = pointer.y - editLibraryRegionY + editLibraryScroll;
    let wantedKind = 'preset';
    if (reorderList === 'edit-song') wantedKind = 'song';
    let targetId = 0;
    let closestDistance = 1000000000;
    for (let index = 0; index < editLibraryItems.length; index++) {
        const item = editLibraryItems[index];
        if (item.kind !== wantedKind) continue;
        if (wantedKind === 'preset' && item.songId !== reorderSongId) continue;
        const distance = Math.abs(contentY - (item.top + item.height * 0.5));
        if (Math.min(distance, closestDistance) === distance && distance !== closestDistance) {
            closestDistance = distance;
            targetId = item.id;
        }
    }
    const current = indexOfRowId(reorderItems, reorderId);
    const target = indexOfRowId(reorderItems, targetId);
    if (current >= 0 && target >= 0 && current !== target) {
        moveInArray(reorderItems, current, target);
        if (reorderList === 'edit-preset') rebuildLivePresetSongOrder(reorderSongId);
        addLog('Moving to position ' + (target + 1), '', COLORS.amber);
    }
}

function startReorder(list: string, id: number, pointer: PointerHit): void {
    const isSong = list === 'song';
    const arr = isSong ? songs : presets;
    const regionY = isSong ? songRegionY : presetRegionY;
    const rowH = isSong ? songRegionRowH : presetRegionRowH;
    const scroll = isSong ? songScroll : presetScroll;
    const idx = indexOfRowId(arr, id);
    if (idx < 0) return;

    reorderActive = true;
    reorderList = list;
    reorderId = id;
    const rowTop = regionY + (idx - scroll) * rowH;
    reorderGrabDY = pointer.y - rowTop;
}

function updateReorderDrag(pointer: PointerHit): void {
    if (reorderList === 'edit-song' || reorderList === 'edit-preset') {
        updateEditReorderDrag(pointer);
        return;
    }
    const isSong = reorderList === 'song';
    const arr = isSong ? songs : presets;
    const regionY = isSong ? songRegionY : presetRegionY;
    const regionH = isSong ? songRegionH : presetRegionH;
    const rowH = isSong ? songRegionRowH : presetRegionRowH;
    const visible = isSong ? songRegionVisible : presetRegionVisible;
    const maxScroll = Math.max(0, arr.length - visible);
    let scroll = isSong ? songScroll : presetScroll;

    /* Auto-scroll when the dragged row nears the top or bottom edge. */
    const edge = dp(REORDER_EDGE_DP);
    if (pointer.y < regionY + edge) {
        scroll = clamp(scroll - REORDER_AUTOSCROLL_ROWS, 0, maxScroll);
    } else if (pointer.y > regionY + regionH - edge) {
        scroll = clamp(scroll + REORDER_AUTOSCROLL_ROWS, 0, maxScroll);
    }
    if (isSong) songScroll = scroll; else presetScroll = scroll;

    /* Target the row the pointer is currently over so dragging onto another
     * item swaps immediately, rather than waiting for the floated row's center
     * to cross a half-row boundary. */
    let target = Math.floor((pointer.y - regionY) / rowH + scroll);
    target = clamp(target, 0, arr.length - 1);
    const current = indexOfRowId(arr, reorderId);
    if (current >= 0 && target !== current) {
        moveInArray(arr, current, target);
        addLog('Moving to position ' + (target + 1), '', COLORS.amber);
    }
}

function finishReorder(): void {
    if (reorderList === 'edit-song') {
        const position = indexOfRowId(songs, reorderId) + 1;
        persistSongOrder();
        loadSongs();
        addLog('Reordered song group', 'New position ' + position, COLORS.green);
        reorderActive = false;
        reorderList = '';
        reorderId = 0;
        reorderSongId = 0;
        reorderItems = [];
        return;
    }
    if (reorderList === 'edit-preset') {
        const position = indexOfRowId(reorderItems, reorderId) + 1;
        persistPresetOrderForSong(reorderSongId);
        loadPresets();
        addLog('Reordered preset', 'New position ' + position, COLORS.green);
        reorderActive = false;
        reorderList = '';
        reorderId = 0;
        reorderSongId = 0;
        reorderItems = [];
        return;
    }
    const position = indexOfRowId(reorderList === 'song' ? songs : presets, reorderId) + 1;
    if (reorderList === 'song') {
        persistSongOrder();
        addLog('Reordered song', 'New position ' + position, COLORS.green);
    } else if (reorderList === 'preset') {
        persistPresetOrder();
        addLog('Reordered preset', 'New position ' + position, COLORS.green);
    }
    reorderActive = false;
    reorderList = '';
    reorderId = 0;
    reorderSongId = 0;
    reorderItems = [];
}

/**
 * Unified touch handling for grouped Edit rows and Live presets: drag to scroll
 * and tap on release to select. Selection happens on release so a drag scrolls
 * the Library instead of selecting the row it started on.
 */
function updateTouch(pointer: PointerHit): void {
    if (modeTransitionActive) {
        touchActive = false;
        return;
    }
    if (editor.active || presetMenu.active || songMenu.active || importDialog.active) {
        touchActive = false;
        if (reorderActive) finishReorder();
        return;
    }

    /* An active reorder drag has its own lifecycle and overrides scroll/tap. */
    if (reorderActive) {
        if (pointer.down) {
            updateReorderDrag(pointer);
        } else {
            finishReorder();
        }
        return;
    }

    if (pointer.pressed) {
        /* Pressing a row's drag handle (edit mode) starts a reorder drag. */
        if (!liveMode && !sequenceRecording) {
            const editSongDrag = buttonIdAt(pointer, 'edit:song:drag:');
            if (editSongDrag.length > 0) {
                startEditReorder('edit-song', 0,
                    Number(editSongDrag.slice('edit:song:drag:'.length)), pointer);
                return;
            }
            const editPresetDrag = buttonIdAt(pointer, 'edit:preset:drag:');
            if (editPresetDrag.length > 0) {
                const parts = editPresetDrag.split(':');
                startEditReorder('edit-preset', Number(parts[3]), Number(parts[4]), pointer);
                return;
            }
            const songDragId = rowIdAt(pointer, 'song:drag:');
            if (songDragId > 0) {
                startReorder('song', songDragId, pointer);
                return;
            }
            const presetDragId = rowIdAt(pointer, 'preset:drag:');
            if (presetDragId > 0) {
                startReorder('preset', presetDragId, pointer);
                return;
            }
        }
        touchList = listRegionAt(pointer);
        touchActive = touchList !== '';
        touchMoved = false;
        touchStartX = pointer.x;
        touchStartY = pointer.y;
        touchStartTime = now();
        touchRowId = 0;
        touchActionId = '';
        if (touchList === 'edit') {
            touchStartScroll = editLibraryScroll;
            const editSong = buttonIdAt(pointer, 'edit:song:select:');
            const editPreset = buttonIdAt(pointer, 'edit:preset:select:');
            if (editSong.length > 0) touchActionId = editSong;
            if (editPreset.length > 0) touchActionId = editPreset;
        } else if (touchList === 'song') {
            touchStartScroll = songScroll;
            touchRowId = rowIdAt(pointer, 'song:select:');
        } else if (touchList === 'preset') {
            touchStartScroll = liveMode ? liveScroll : presetScroll;
            if (liveMode) {
                const liveButton = hitButton(pointer);
                if (liveButton && liveButton.id.indexOf('live:preset:') === 0) {
                    touchActionId = liveButton.id;
                }
            } else {
                touchRowId = rowIdAt(pointer, 'preset:select:');
            }
        }
        return;
    }

    if (!touchActive) return;

    if (pointer.down) {
        const dx = pointer.x - touchStartX;
        const dy = pointer.y - touchStartY;
        const moveThreshold = dp(TOUCH_MOVE_DP);
        if (!touchMoved && dx * dx + dy * dy > moveThreshold * moveThreshold) {
            touchMoved = true;
        }
        if (touchMoved) {
            applyTouchScroll(pointer.y);
        } else if (!liveMode && touchList === 'preset' && touchRowId > 0 &&
            now() - touchStartTime >= LONG_PRESS_MS) {
            openPresetMenu(touchRowId);
            touchActive = false;
        } else if (!liveMode && touchList === 'song' && touchRowId > 0 &&
            now() - touchStartTime >= LONG_PRESS_MS) {
            openSongMenu(touchRowId);
            touchActive = false;
        }
        return;
    }

    /* Released: a tap (no drag) selects the row under the original press. */
    if (!touchMoved && touchRowId > 0) {
        if (touchList === 'song') handleNormalButton('song:select:' + touchRowId);
        else if (touchList === 'preset') handleNormalButton('preset:select:' + touchRowId);
    } else if (!touchMoved && touchActionId.length > 0) {
        handleNormalButton(touchActionId);
    }
    touchActive = false;
}

function handleClick(pointer: PointerHit): void {
    if (modeTransitionActive) return;
    if (!pointer.pressed) return;
    if (reorderActive) return;
    const button = hitButton(pointer);
    if (editor.active) {
        if (button) handleEditorButton(button.id);
        return;
    }
    if (importDialog.active) {
        if (button) handleImportButton(button.id);
        return;
    }
    if (presetMenu.active) {
        if (button) handlePresetMenuButton(button.id);
        return;
    }
    if (songMenu.active) {
        if (button) handleSongMenuButton(button.id);
        return;
    }
    if (!button) return;
    /* Song/preset row selection is resolved on release by updateTouch so a
     * drag scrolls the list instead of selecting on touchdown. */
    if (button.id.indexOf('song:select:') === 0 ||
        button.id.indexOf('preset:select:') === 0 ||
        button.id.indexOf('edit:song:select:') === 0 ||
        button.id.indexOf('edit:preset:select:') === 0 ||
        button.id.indexOf('live:preset:') === 0) return;
    handleNormalButton(button.id);
}

function handleKeyboardShortcuts(): void {
    if (modeTransitionActive) return;
    if (editor.active) {
        handleEditorKeyboard();
        return;
    }
    if (importDialog.active) {
        if (sys.input.isKeyPressed(SDL_ESCAPE)) closeImportDialog();
        return;
    }
    if (presetMenu.active) {
        if (sys.input.isKeyPressed(SDL_ESCAPE)) closePresetMenu();
        return;
    }
    if (songMenu.active) {
        if (sys.input.isKeyPressed(SDL_ESCAPE)) closeSongMenu();
        return;
    }
    if (liveMode) {
        if (sys.input.isKeyPressed(SDL_ENTER) && selectedPresetId > 0) recallPreset(selectedPresetId);
        return;
    }
    if (sequenceRecording) return;
    if (sys.input.isKeyPressed(SDL_X)) clearCapture();
    if (sys.input.isKeyPressed(SDL_ENTER) && selectedPresetId > 0) recallPreset(selectedPresetId);
}

function frame(timestamp: number): void {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();
    const pointer = getPointer(input);

    handleEditorTextInput(input);
    handleKeyboardShortcuts();
    updateSequencePlayback();
    updateSequencePanelAnimation();
    updateModeTransition();
    render(width, height, pointer);
    handleListWheel(input, pointer);
    updateTouch(pointer);
    handleClick(pointer);

    sys.animation.requestFrame(frame);
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