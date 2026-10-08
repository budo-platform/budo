/// <reference path="./budo.d.ts" />
/**
 * File Explorer - Budo
 *
 * Demonstrates several Budo APIs working together, with a budo-ui interface:
 *   - sys.files exposes the virtual assets/ and files/ mounts.
 *   - sys.gl can upload decodable file bytes as textures.
 *   - sys.audio decodes audio files a block at a time (any size, seekable)
 *     and plays them through an output stream that the app fills.
 *   - ui/ (budo-ui) draws the table of entries, the filter field, the
 *     resizable split, and the preview pane, with keyboard navigation,
 *     screen-reader support, and motion.
 */

import { createUI, themes } from './ui/budo-ui.js';

class ShaderProgram {
    programId

    constructor(programId) {
        this.programId = programId
        if (this.programId < 0) throw "invalid program id"
    }

    use() {
        if (sys.gl.useProgram(this.programId) < 0) throw "cannot use program"
        return this
    }

    texture(name, canvasTextureOrTextureId, textureUnit) {
        sys.gl.texture(this.programId, name, canvasTextureOrTextureId, textureUnit)
        return this
    }

    drawRegion(x, y, w, h, targetId) {
        sys.gl.drawRegionImmediate(this.programId, x, y, w, h, targetId)
        return this
    }
}

// The explorer's own dark palette (Catppuccin Mocha); the sun button switches to the light theme.
const DARK = {
    ...themes.dark,
    background: '#1E1E2E', surface: '#181825', raised: '#313244', ink: '#CDD6F4', muted: '#7F849C',
    border: '#45475A', accent: '#89B4FA', accentInk: '#11111B', highlight: '#FAB387', danger: '#F38BA8',
    focus: '#89B4FA', shadow: '#00000080',
};
const COLORS = { folder: '#F9E2AF', image: '#89B4FA', audio: '#A6E3A1', previewBg: '#11111B' };

const ZOOM_MIN = 0.75;
const ZOOM_MAX = 2;
const ZOOM_STEP = 0.125;
const MAX_TEXT_PREVIEW_LINES = 200;
const MAX_TEXT_PREVIEW_BYTES = 2 * 1024 * 1024;
const MAX_IMAGE_PREVIEW_BYTES = 32 * 1024 * 1024;
const DEFAULT_AUDIO_GAIN = 0.85;

const SCANCODE = {
    F: 9, H: 11, L: 15, R: 21, ESCAPE: 41, BACKSPACE: 42, SPACE: 44, MINUS: 45, EQUALS: 46,
    RIGHT: 79, LEFT: 80,
};

const TEXT_EXTENSIONS = [
    '.txt', '.md', '.js', '.ts', '.json', '.xml', '.html', '.css',
    '.c', '.h', '.cpp', '.hpp', '.py', '.rb', '.rs', '.go', '.java',
    '.sh', '.bat', '.yml', '.yaml', '.toml', '.ini', '.cfg', '.conf',
    '.log', '.csv', '.svg', '.wat', '.vert', '.frag', '.glsl',
    '.cmake', '.gradle', '.properties', '.gitignore', '.editorconfig',
];
const IMAGE_EXTENSIONS = ['.png', '.jpg', '.jpeg', '.bmp', '.tga', '.gif'];
const AUDIO_EXTENSIONS = ['.wav', '.mp3', '.ogg', '.flac'];

const ui = createUI();
ui.registerIcon('image', 'M4 5h16v14H4zM4 16l4.5-4.5 4 4 3-3 4.5 4.5M16 9.5a1.5 1.5 0 1 1-3 0a1.5 1.5 0 1 1 3 0z');
ui.registerIcon('audio', 'M9 17.5V6l10-2v11.5M9 17.5a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0zM19 15.5a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0z');
ui.registerIcon('play', 'M8 5.5v13l10.5-6.5z', { fill: true });
ui.registerIcon('pause', 'M8.5 5.5v13M15.5 5.5v13');

// Text previews use the bundled monospace font (JetBrains Mono, OFL; see JetBrainsMono-OFL.txt).
let monoFont = null;
try {
    sys.font.load('JetBrainsMono-Regular.ttf', 'mono');
    monoFont = 'mono';
} catch (e) { }

const state = {
    currentPath: '',
    allEntries: [],
    entries: [],
    filter: { value: '' },
    appliedFilter: '',
    selectedId: null,
    sort: { key: 'name', descending: false },
    history: [''],
    historyIndex: 0,
    dark: true,
    zoom: 1,
    preview: emptyPreview(),
    previewSerial: 0,
};

let imageShader = null;
let imageRect = null; // where the GL image goes this frame, after the canvas

function emptyPreview() {
    return {
        mode: 'empty', name: '', path: '', size: 0, text: { value: '' }, message: '',
        imageTexture: null, imageInfo: null,
        // Audio: a decoder reads the file in blocks; an output stream plays
        // what the decoder makes, filled by its callback.
        audioDecoder: null, audioStream: null, audioInfo: null, audioLoop: false, audioEnded: false,
    };
}

/** Theme sizes follow the zoom (+ and - keys); colors animate on a switch. */
function applyTheme() {
    const base = state.dark ? DARK : themes.light;
    ui.setTheme({ ...base, font: base.font * state.zoom, small: base.small * state.zoom, row: base.row * state.zoom });
}

// ── Directories ──────────────────────────────────────────────────────────

const entryId = entry => (entry.type === 'directory' ? 'd:' : 'f:') + entry.name;

function sortEntries(entries) {
    const { key, descending } = state.sort;
    const sign = descending ? -1 : 1;
    const compare = (a, b) => key === 'size'
        ? sign * ((a.size || 0) - (b.size || 0)) || a.name.localeCompare(b.name)
        : sign * a.name.localeCompare(b.name);
    // Folders stay first whatever the sort.
    return [
        ...entries.filter(entry => entry.type === 'directory').sort(compare),
        ...entries.filter(entry => entry.type !== 'directory').sort(compare),
    ];
}

function applyFilter() {
    state.appliedFilter = state.filter.value;
    const query = state.filter.value.trim().toLocaleLowerCase();
    state.entries = query
        ? state.allEntries.filter(entry => entry.name.toLocaleLowerCase().includes(query))
        : state.allEntries.slice();
    if (!state.entries.some(entry => entryId(entry) === state.selectedId)) select(null);
}

function showDirectory(path, entries) {
    state.currentPath = path;
    state.allEntries = sortEntries(entries);
    state.filter.value = '';
    select(null);
    applyFilter();
}

function loadDirectory(path) {
    return path ? sys.files.list(path) : sys.files.list();
}

function navigateTo(path) {
    try {
        const entries = loadDirectory(path);
        if (state.currentPath !== path) {
            state.history = state.history.slice(0, state.historyIndex + 1);
            state.history.push(path);
            state.historyIndex = state.history.length - 1;
        }
        showDirectory(path, entries);
    } catch (e) {
        ui.toast('Cannot list ' + (path || '/') + ': ' + (e.message || e), { kind: 'danger' });
    }
}

function goToHistory(index) {
    if (index < 0 || index >= state.history.length) return;
    try {
        showDirectory(state.history[index], loadDirectory(state.history[index]));
        state.historyIndex = index;
    } catch (e) {
        ui.toast('Error: ' + (e.message || e), { kind: 'danger' });
    }
}

const goBack = () => goToHistory(state.historyIndex - 1);
const goForward = () => goToHistory(state.historyIndex + 1);
const goHome = () => navigateTo('');

function goUp() {
    const parts = state.currentPath.split('/').filter(Boolean);
    parts.pop();
    navigateTo(parts.join('/'));
}

function getEntryPath(entry) {
    return state.currentPath ? state.currentPath + '/' + entry.name : entry.name;
}

const entryById = id => state.entries.find(entry => entryId(entry) === id) || null;

/** Select an entry (or nothing): files show in the preview. */
function select(id) {
    if (id === state.selectedId) return;
    state.selectedId = id;
    const entry = id ? entryById(id) : null;
    if (entry && entry.type === 'file') previewFile(entry);
    else clearPreview();
}

/** Open an entry: folders navigate, audio files start playing. */
function open(id) {
    const entry = entryById(id);
    if (!entry) return;
    if (entry.type === 'directory') navigateTo(getEntryPath(entry));
    else if (state.preview.mode === 'audio') toggleAudioPlayback();
}

// ── Previews ─────────────────────────────────────────────────────────────

function clearPreview() {
    const preview = state.preview;
    stopAudio();
    if (preview.audioDecoder !== null) sys.audio.closeDecoder(preview.audioDecoder);
    if (preview.imageTexture !== null) {
        try { sys.gl.destroyTexture(preview.imageTexture); } catch (e) { }
    }
    state.preview = emptyPreview();
}

function previewFile(entry) {
    clearPreview();
    const preview = state.preview;
    const path = getEntryPath(entry);
    preview.name = entry.name;
    preview.path = path;
    preview.size = entry.size !== undefined ? entry.size : 0;
    state.previewSerial++;

    // Audio streams from the file, so any size plays; text and images are read whole.
    const limit = isTextFile(entry.name) ? MAX_TEXT_PREVIEW_BYTES : isImageFile(entry.name) ? MAX_IMAGE_PREVIEW_BYTES : 0;
    if (limit > 0 && preview.size > limit) {
        preview.mode = 'message';
        preview.message = 'Preview unavailable: ' + formatSize(preview.size) + ' exceeds the ' +
            formatSize(limit) + ' preview limit.';
        return;
    }
    if (isImageFile(entry.name)) return previewImage(path);
    if (isAudioFile(entry.name)) return previewAudio(path);
    if (!isTextFile(entry.name)) {
        preview.mode = 'message';
        preview.message = 'Binary file, ' + formatSize(preview.size) + '.';
        return;
    }
    try {
        const text = sys.files.readText(path);
        const lines = text.split('\n');
        preview.mode = 'text';
        preview.text.value = lines.length > MAX_TEXT_PREVIEW_LINES
            ? lines.slice(0, MAX_TEXT_PREVIEW_LINES).join('\n') + '\n\n... (' + lines.length + ' lines in total)'
            : text;
    } catch (e) {
        preview.mode = 'message';
        preview.message = 'Cannot read the file: ' + (e.message || e);
    }
}

function previewImage(path) {
    const preview = state.preview;
    preview.mode = 'image';
    try {
        const imageBytes = sys.files.readBinary(path);
        preview.imageInfo = readImageInfo(path, imageBytes);
        // Decode the browsed file bytes directly instead of resolving a project asset path.
        preview.imageTexture = sys.gl.loadTexture2DFromBuffer(imageBytes);
    } catch (e) {
        preview.mode = 'message';
        preview.message = 'Image file, ' + formatSize(preview.size) + '. sys.files can read it, but its bytes ' +
            'could not be decoded into a GL texture: ' + (e.message || e);
    }
}

function previewAudio(path) {
    const preview = state.preview;
    preview.mode = 'audio';
    // Decode at the device's rate, ready for an output stream.
    const decoder = sys.audio.openDecoder(path, { sampleRate: sys.audio.getSampleRate(), channels: 2 });
    if (decoder < 0) {
        preview.mode = 'message';
        preview.message = 'Audio file, ' + formatSize(preview.size) + '. Budo decodes WAV, MP3, Ogg Vorbis, and ' +
            'FLAC a block at a time with sys.audio.openDecoder(). Decoder message: ' + sys.audio.getError();
        return;
    }
    preview.audioDecoder = decoder;
    preview.audioInfo = sys.audio.getDecoderInfo(decoder);
}

/** The output stream's callback: decode the next chunk straight into it. */
function fillAudio(buffer) {
    const preview = state.preview;
    let frames = sys.audio.decode(preview.audioDecoder, buffer);
    // At the end: loop back to the start, or stop after this chunk.
    if (frames >= 0 && frames * 2 < buffer.length && preview.audioLoop && sys.audio.seekDecoder(preview.audioDecoder, 0))
        frames += Math.max(0, sys.audio.decode(preview.audioDecoder, buffer.subarray(frames * 2)));
    if (frames * 2 < buffer.length) preview.audioEnded = true;
    for (let i = 0; i < buffer.length; i++) buffer[i] *= DEFAULT_AUDIO_GAIN;
}

function playAudio() {
    const preview = state.preview;
    if (preview.audioDecoder === null || preview.audioStream !== null) return;
    if (preview.audioEnded || sys.audio.getDecoderInfo(preview.audioDecoder).ended) {
        sys.audio.seekDecoder(preview.audioDecoder, 0);
        preview.audioEnded = false;
    }
    preview.audioStream = sys.audio.openOutput({ channels: 2 }, fillAudio);
    if (preview.audioStream < 0) {
        preview.audioStream = null;
        ui.toast('Cannot play: ' + sys.audio.getError(), { kind: 'danger' });
    }
}

/** Pause: the decoder keeps its position for the next play. */
function stopAudio() {
    const preview = state.preview;
    if (preview.audioStream === null) return;
    sys.audio.closeOutput(preview.audioStream);
    preview.audioStream = null;
}

function toggleAudioPlayback() {
    if (state.preview.audioStream !== null) stopAudio();
    else playAudio();
}

function setAudioLoop(loop) {
    state.preview.audioLoop = loop;
}

/** Where playback is: what was decoded, minus what still waits in the queue. */
function audioPosition() {
    const preview = state.preview;
    const info = sys.audio.getDecoderInfo(preview.audioDecoder);
    const queued = preview.audioStream !== null ? 0.04 : 0;
    return Math.max(0, info.position - queued);
}

function formatTime(seconds) {
    if (!(seconds >= 0)) return '--:--';
    const whole = Math.floor(seconds);
    return Math.floor(whole / 60) + ':' + String(whole % 60).padStart(2, '0');
}

function getExtension(name) {
    return name.lastIndexOf('.') >= 0 ? name.substring(name.lastIndexOf('.')).toLowerCase() : '';
}
const isTextFile = name => TEXT_EXTENSIONS.includes(getExtension(name)) || getExtension(name) === '';
const isImageFile = name => IMAGE_EXTENSIONS.includes(getExtension(name));
const isAudioFile = name => AUDIO_EXTENSIONS.includes(getExtension(name));

function readImageInfo(path, imageBytes) {
    try {
        // Header parsing keeps metadata available even when GL cannot decode the image.
        const bytes = new Uint8Array(imageBytes);
        if (bytes.length >= 24 && bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4E && bytes[3] === 0x47)
            return { type: 'PNG', width: readU32BE(bytes, 16), height: readU32BE(bytes, 20) };
        if (bytes.length >= 10 && bytes[0] === 0x47 && bytes[1] === 0x49 && bytes[2] === 0x46)
            return { type: 'GIF', width: readU16LE(bytes, 6), height: readU16LE(bytes, 8) };
        if (bytes.length >= 26 && bytes[0] === 0x42 && bytes[1] === 0x4D)
            return { type: 'BMP', width: readU32LE(bytes, 18), height: Math.abs(readI32LE(bytes, 22)) };
        if (bytes.length >= 18 && getExtension(path) === '.tga')
            return { type: 'TGA', width: readU16LE(bytes, 12), height: readU16LE(bytes, 14) };
        return readJpegInfo(bytes);
    } catch (e) {
        return null;
    }
}

function readJpegInfo(bytes) {
    if (bytes.length < 4 || bytes[0] !== 0xFF || bytes[1] !== 0xD8) return null;
    let i = 2;
    while (i + 9 < bytes.length) {
        if (bytes[i] !== 0xFF) {
            i++;
            continue;
        }
        const marker = bytes[i + 1];
        const length = readU16BE(bytes, i + 2);
        if (length < 2) return null;
        if (marker >= 0xC0 && marker <= 0xCF && marker !== 0xC4 && marker !== 0xC8 && marker !== 0xCC)
            return { type: 'JPEG', width: readU16BE(bytes, i + 7), height: readU16BE(bytes, i + 5) };
        i += 2 + length;
    }
    return null;
}

function readU16BE(bytes, offset) { return (bytes[offset] << 8) | bytes[offset + 1]; }
function readU16LE(bytes, offset) { return bytes[offset] | (bytes[offset + 1] << 8); }
function readU32BE(bytes, offset) { return ((bytes[offset] << 24) | (bytes[offset + 1] << 16) | (bytes[offset + 2] << 8) | bytes[offset + 3]) >>> 0; }
function readU32LE(bytes, offset) { return (bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24)) >>> 0; }
function readI32LE(bytes, offset) { return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24); }

function formatSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
}

function previewMeta() {
    const preview = state.preview;
    if (preview.mode === 'image' && preview.imageInfo)
        return preview.imageInfo.type + '  ' + preview.imageInfo.width + '×' + preview.imageInfo.height + '  ' + formatSize(preview.size);
    if (preview.mode === 'audio' && preview.audioInfo) {
        const info = preview.audioInfo;
        const channels = info.sourceChannels === 1 ? 'mono' : info.sourceChannels === 2 ? 'stereo' : info.sourceChannels + ' ch';
        return getExtension(preview.name).substring(1).toUpperCase() + '  ' + (info.sourceSampleRate / 1000).toFixed(1) +
            ' kHz  ' + channels + '  ' + formatSize(preview.size);
    }
    return preview.size > 0 ? formatSize(preview.size) : '';
}

// ── Interface ────────────────────────────────────────────────────────────

/** Shorten `text` with an ellipsis to fit `width` at `size`. */
function fit(text, width, size) {
    if (ui.textWidth(text, size) <= width) return text;
    let end = text.length;
    while (end > 0 && ui.textWidth(text.slice(0, end) + '…', size) > width) end--;
    return end > 0 ? text.slice(0, end) + '…' : '';
}

function iconFor(entry) {
    if (entry.type === 'directory') return ['folder', COLORS.folder];
    if (isImageFile(entry.name)) return ['image', COLORS.image];
    if (isAudioFile(entry.name)) return ['audio', COLORS.audio];
    return ['file', ui.colors.muted];
}

function drawHeader(rect) {
    const dp = ui.dp, z = state.zoom;
    const size = ui.sp(ui.theme.small);
    const count = state.appliedFilter
        ? state.entries.length + ' of ' + state.allEntries.length + ' items'
        : state.entries.length + ' items';
    const title = 'File Explorer';
    const [titleRect, pathRect, countRect, themeRect] = ui.columns(rect,
        [{ content: ui.textWidth(title, ui.sp(ui.theme.font + 2)) }, { weight: 1 }, { content: ui.textWidth(count, size) },
            dp(40 * z)], dp(14));
    ui.label(title, { ...titleRect, y: titleRect.y + (titleRect.height - ui.sp(ui.theme.font + 2) * 1.25) / 2 },
        { size: ui.sp(ui.theme.font + 2), color: ui.colors.accent });
    const path = '/ ' + (state.currentPath || '');
    ui.label(fit(path, pathRect.width, size), { ...pathRect, y: pathRect.y + (pathRect.height - size * 1.25) / 2 }, { size });
    ui.label(count, { ...countRect, y: countRect.y + (countRect.height - size * 1.25) / 2 }, { size, color: ui.colors.muted });
    if (ui.iconButton('theme', state.dark ? 'sun' : 'moon', themeRect, { label: state.dark ? 'Light theme' : 'Dark theme' })) {
        state.dark = !state.dark;
        applyTheme();
    }
}

function drawToolbar(rect) {
    const dp = ui.dp, button = dp(40 * state.zoom);
    const [back, forward, up, home, refresh, filter] = ui.columns(rect,
        [button, button, button, button, button, { weight: 1 }], dp(6));
    const enabledFor = condition => ({ enabled: condition });
    if (ui.iconButton('back', 'arrowLeft', back, { label: 'Back', ...enabledFor(state.historyIndex > 0) })) goBack();
    if (ui.iconButton('forward', 'arrowRight', forward, { label: 'Forward', ...enabledFor(state.historyIndex < state.history.length - 1) })) goForward();
    if (ui.iconButton('up', 'chevronUp', up, { label: 'Parent folder', ...enabledFor(state.currentPath !== '') })) goUp();
    if (ui.iconButton('home', 'home', home, { label: 'Root', ...enabledFor(state.currentPath !== '') })) goHome();
    if (ui.iconButton('refresh', 'refresh', refresh, { label: 'Refresh' })) navigateTo(state.currentPath);
    ui.field('filter', filter, state.filter, { placeholder: 'Filter (F)', label: 'Filter' });
    if (state.filter.value !== state.appliedFilter) applyFilter();
}

function drawEntries(rect) {
    const dp = ui.dp, z = state.zoom;
    const size = ui.sp(ui.theme.small);
    const result = ui.table('entries', rect, {
        label: 'Files',
        columns: [
            { key: 'name', label: 'Name', sortable: true, width: { weight: 1 } },
            { key: 'size', label: 'Size', sortable: true, align: 'right', width: 96 * z },
        ],
        rowCount: state.entries.length,
        row: index => state.entries[index],
        rowId: index => entryId(state.entries[index]),
        selectedId: state.selectedId,
        sort: state.sort,
        rowHeight: dp(40 * z),
        emptyText: state.appliedFilter ? 'No matching files' : 'This folder is empty',
        cell: (column, entry, cell) => {
            const y = cell.y + cell.height / 2 + size * 0.35;
            if (column.key === 'size') {
                const text = entry.type === 'directory' ? 'Folder' : formatSize(entry.size || 0);
                ui.text(text, cell.x + cell.width - dp(10) - ui.textWidth(text, size), y, size, ui.colors.muted);
                return true;
            }
            const [icon, color] = iconFor(entry);
            const iconSize = dp(18 * z);
            ui.icon(icon, { x: cell.x + dp(10), y: cell.y + (cell.height - iconSize) / 2, width: iconSize, height: iconSize }, { color });
            const x = cell.x + dp(18) + iconSize;
            ui.text(fit(entry.name, cell.x + cell.width - x - dp(6), size), x, y, size,
                entry.type === 'directory' ? ui.colors.accent : ui.colors.ink);
            return true;
        },
    });
    if (result.sort) {
        state.sort = result.sort;
        state.allEntries = sortEntries(state.allEntries);
        applyFilter();
    }
    select(result.selectedId);
    if (result.activatedId) open(result.activatedId);
}

function drawPreview(rect) {
    const dp = ui.dp, z = state.zoom, preview = state.preview;
    const size = ui.sp(ui.theme.small);
    ui.fill(rect, ui.colors.surface);
    if (preview.mode === 'empty') {
        const hint = 'Select a file to preview it. Double-click or Enter opens a folder.';
        const { height } = ui.measureParagraph(hint, rect.width - dp(48));
        ui.paragraph(hint, { x: rect.x + dp(24), y: rect.y + (rect.height - height) / 2, width: rect.width - dp(48), height },
            { color: ui.colors.muted, align: 'center' });
        return;
    }
    const [header, body] = ui.rows(rect, [dp(44 * z), { weight: 1 }], 0);
    ui.fill(header, ui.colors.raised, 0);
    const meta = previewMeta();
    const metaWidth = ui.textWidth(meta, size);
    const textY = header.y + (header.height - size * 1.25) / 2;
    ui.label(fit(preview.name, header.width - metaWidth - dp(36), size), { x: header.x + dp(12), y: textY, width: header.width, height: size * 1.25 },
        { size, color: ui.colors.accent });
    ui.label(meta, { x: header.x + header.width - metaWidth - dp(12), y: textY, width: metaWidth, height: size * 1.25 },
        { size, color: ui.colors.muted });
    const inner = ui.inset(body, dp(12));

    if (preview.mode === 'text') {
        // A read-only text area: wraps, scrolls, selects, and copies.
        ui.textArea('preview:' + state.previewSerial, inner, preview.text, {
            readOnly: true, label: preview.name, font: monoFont || undefined, size: ui.sp(ui.theme.small) * 0.85,
        });
    } else if (preview.mode === 'image') {
        ui.fill(body, COLORS.previewBg, 0);
        const info = preview.imageInfo || { width: 1, height: 1 };
        const scale = Math.min(inner.width / info.width, inner.height / info.height);
        const width = Math.max(1, Math.round(info.width * scale)), height = Math.max(1, Math.round(info.height * scale));
        imageRect = { x: Math.round(inner.x + (inner.width - width) / 2), y: Math.round(inner.y + (inner.height - height) / 2), width, height };
    } else if (preview.mode === 'audio') {
        drawAudioPreview(inner);
    } else {
        ui.paragraph(preview.message, inner, { color: ui.colors.muted });
    }
}

function drawAudioPreview(rect) {
    const dp = ui.dp, z = state.zoom, preview = state.preview;
    // The callback reached the end: close the stream (not from inside it).
    if (preview.audioEnded && preview.audioStream !== null && !preview.audioLoop) stopAudio();
    const playing = preview.audioStream !== null;
    const duration = preview.audioInfo ? preview.audioInfo.duration : -1;
    const button = Math.min(dp(88 * z), rect.width * 0.4, rect.height * 0.4);
    const columnWidth = Math.min(rect.width, dp(360 * z));
    const column = { x: rect.x + (rect.width - columnWidth) / 2, width: columnWidth };
    const total = button + dp(24 + 44 + 30 + 48 + 30) * z;
    let y = rect.y + Math.max(0, (rect.height - total) / 2);
    if (ui.iconButton('play', playing ? 'pause' : 'play', { x: rect.x + (rect.width - button) / 2, y, width: button, height: button },
        { label: playing ? 'Pause' : 'Play', variant: 'primary' })) toggleAudioPlayback();
    y += button + dp(24 * z);

    // Seek bar: drag to jump anywhere in the file.
    const position = audioPosition();
    if (duration > 0) {
        const picked = ui.slider('seek', { ...column, y, height: dp(44 * z) }, Math.min(position, duration),
            { min: 0, max: duration, label: 'Position' });
        if (Math.abs(picked - position) > 0.25) {
            sys.audio.seekDecoder(preview.audioDecoder, picked);
            preview.audioEnded = false;
        }
    }
    y += dp(44 * z);
    const size = ui.sp(ui.theme.font);
    const times = formatTime(position) + ' / ' + formatTime(duration);
    ui.label(times, { x: rect.x + (rect.width - ui.textWidth(times, size)) / 2, y, width: rect.width, height: dp(30 * z) }, { size });
    y += dp(30 * z);
    setAudioLoop(ui.toggle('loop', 'Loop', { ...column, y, height: dp(48 * z) }, preview.audioLoop));
    y += dp(48 * z);
    const hint = 'Space plays and pauses, L loops';
    const small = ui.sp(ui.theme.small);
    ui.label(hint, { x: rect.x + (rect.width - ui.textWidth(hint, small)) / 2, y, width: rect.width, height: dp(30 * z) },
        { size: small, color: ui.colors.muted });
    // Keep the position moving while it plays.
    if (playing) ui.wakeAfter(0.25);
}

/** Shortcuts while no text field is being edited. */
function handleShortcuts() {
    const pressed = key => sys.input.isKeyPressed(key);
    if (ui.focus === 'filter') {
        if (pressed(SCANCODE.ESCAPE)) {
            state.filter.value = '';
            ui.focus = null;
            sys.input.stopTextInput();
        }
        return;
    }
    if (ui.focus !== null) return;
    const keyboard = ui.input.keyboard || {};
    if (pressed(SCANCODE.ESCAPE)) {
        if (state.filter.value) state.filter.value = '';
        else goUp();
    }
    if (pressed(SCANCODE.LEFT) || pressed(SCANCODE.BACKSPACE)) goBack();
    if (pressed(SCANCODE.RIGHT) && state.selectedId) open(state.selectedId);
    if (pressed(SCANCODE.F) && !keyboard.ctrl && !keyboard.meta) ui.focusField('filter', state.filter);
    if (pressed(SCANCODE.R)) navigateTo(state.currentPath);
    if (pressed(SCANCODE.H)) goHome();
    // (Space on the focused play button already clicks it.)
    if (pressed(SCANCODE.SPACE) && state.preview.mode === 'audio' && ui.keyFocus !== 'play') toggleAudioPlayback();
    if (pressed(SCANCODE.L) && state.preview.mode === 'audio') setAudioLoop(!state.preview.audioLoop);
    if (pressed(SCANCODE.EQUALS) || pressed(SCANCODE.MINUS)) {
        const step = pressed(SCANCODE.EQUALS) ? ZOOM_STEP : -ZOOM_STEP;
        state.zoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, state.zoom + step));
        applyTheme();
    }
}

function frame() {
    ui.begin(sys.input.get());
    ui.clear();
    imageRect = null;
    const dp = ui.dp, z = state.zoom;
    const page = ui.inset(ui.bounds, dp(12));
    const [header, toolbar, body] = ui.rows(page, [dp(44 * z), dp(44 * z), { weight: 1 }], dp(10));
    drawHeader(header);
    drawToolbar(toolbar);
    const narrow = page.width < dp(640);
    ui.split('main', body, drawEntries, drawPreview,
        { vertical: narrow, ratio: narrow ? 0.45 : 0.4, min: 0.25, max: 0.75 });
    handleShortcuts();
    ui.end();

    // The image preview draws with GL over the canvas.
    if (imageRect && state.preview.imageTexture !== null) {
        if (imageShader === null) imageShader = new ShaderProgram(sys.gl.createProgram('image_preview.vert', 'image_preview.frag'));
        sys.gl.bindScreen();
        imageShader.use().texture('u_image', state.preview.imageTexture, 1)
            .drawRegion(imageRect.x, imageRect.y, imageRect.width, imageRect.height, -1);
    }
    // Draw again while something moves, otherwise on the next input.
    ui.nextFrame(frame);
}

applyTheme();
navigateTo('');
sys.animation.requestFrame(frame);
