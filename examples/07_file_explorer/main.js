/**
 * File Explorer - Budo
 *
 * Demonstrates several small Budo APIs working together:
 *   - sys.files exposes the virtual assets/ and files/ mounts.
 *   - sys.gl can upload decodable file bytes as textures.
 *   - sys.audio can decode and play audio bytes read from files.
 */

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

    uniform1i(name, value) {
        sys.gl.setUniform1i(this.programId, name, value)
        return this
    }

    uniform1f(name, value) {
        sys.gl.setUniform1f(this.programId, name, value)
        return this
    }

    uniform2f(name, value0, value1) {
        sys.gl.setUniform2f(this.programId, name, value0, value1)
        return this
    }

    uniform3f(name, v0, v1, v2) {
        sys.gl.setUniform3f(this.programId, name, v0, v1, v2)
        return this
    }

    uniform4f(name, v0, v1, v2, v3) {
        sys.gl.setUniform4f(this.programId, name, v0, v1, v2, v3)
        return this
    }

    uniformMatrix4(name, mat16) {
        sys.gl.setUniformMatrix4(this.programId, name, mat16)
        return this
    }

    texture(name, canvasTextureOrTextureId, textureUnit) {
        sys.gl.texture(this.programId, name, canvasTextureOrTextureId, textureUnit)
        return this
    }

    // only called from magneto
    renderTargetTexture(name, targetId, textureUnit) {
        sys.gl.renderTargetTexture(this.programId, name, targetId, textureUnit)
        return this
    }

    // in the original c wrapper program.drawFullscreen was calling gl.drawFullscreenImmediate
    drawFullscreen() {
        sys.gl.drawFullscreenImmediate(this.programId)
        return this
    }

    drawRegion(x, y, w, h, targetId) {
        sys.gl.drawRegionImmediate(this.programId, x, y, w, h, targetId)
        return this
    }

    // in the original c wrapper program.drawMesh was calling gl.drawMesgImmediate
    drawMesh(layoutId, options) {
        sys.gl.drawMeshImmediate(this.programId, layoutId, options)
        return this
    }
}

const COLORS = {
    bg: '#1E1E2E',
    sidebar: '#181825',
    header: '#313244',
    headerText: '#CDD6F4',
    text: '#CDD6F4',
    textMuted: '#6C7086',
    textPreview: '#BAC2DE',
    accent: '#89B4FA',
    accentHover: '#B4D0FB',
    folder: '#F9E2AF',
    file: '#A6E3A1',
    selected: '#45475A',
    hover: '#313244',
    border: '#45475A',
    scrollbar: '#585B70',
    error: '#F38BA8',
    success: '#A6E3A1',
    warning: '#FAB387',
    previewBg: '#11111B',
    divider: '#6C7086',
};

const BASE_HEADER_HEIGHT = 48;
const BASE_ROW_HEIGHT = 32;
const BASE_SIDEBAR_MIN_WIDTH = 280;
const SIDEBAR_RATIO = 0.35;
const BASE_PADDING = 12;
const BASE_ICON_SIZE = 16;
const SCROLLBAR_WIDTH = 8;
const DIVIDER_WIDTH = 6;
const DIVIDER_HIT_WIDTH = 18;
const NAV_BUTTON_COUNT = 5;
const PREVIEW_MIN_WIDTH = 240;
const TOUCH_SCROLL_SCALE = 1.0;
const MAX_TEXT_PREVIEW_LINES = 200;
const MAX_TEXT_PREVIEW_BYTES = 2 * 1024 * 1024;
const MAX_MEDIA_PREVIEW_BYTES = 32 * 1024 * 1024;
const DEFAULT_AUDIO_GAIN = 0.85;

const INITIAL_FONT_SCALE = 2.0;
const FONT_SCALE_MIN = 0.5;
const FONT_SCALE_MAX = 4.0;
const FONT_SCALE_STEP = 0.25;

const SCANCODE = {
    F: 9,
    H: 11,
    L: 15,
    RETURN: 40,
    ESCAPE: 41,
    BACKSPACE: 42,
    SPACE: 44,
    MINUS: 45,
    EQUALS: 46,
    R: 21,
    RIGHT: 79,
    LEFT: 80,
    DOWN: 81,
    UP: 82,
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

let fontScale = INITIAL_FONT_SCALE;

let HEADER_HEIGHT, ROW_HEIGHT, PADDING, ICON_SIZE;

function updateLayout() {
    HEADER_HEIGHT = Math.round(BASE_HEADER_HEIGHT * fontScale);
    ROW_HEIGHT = Math.round(BASE_ROW_HEIGHT * fontScale);
    PADDING = Math.round(BASE_PADDING * fontScale);
    ICON_SIZE = Math.round(BASE_ICON_SIZE * fontScale);
}

const state = {
    currentPath: '',
    allEntries: [],
    entries: [],
    filterQuery: '',
    filterActive: false,
    filterSelectionStart: 0,
    filterSelectionEnd: 0,
    rowVisuals: new Map(),
    selectedIndex: -1,
    hoveredIndex: -1,
    scrollY: 0,
    previewText: null,
    previewName: '',
    previewScroll: 0,
    errorMessage: '',
    history: [''],
    historyIndex: 0,
    sidebarRatio: SIDEBAR_RATIO,
    resizingSidebar: false,
    hoveringDivider: false,
    previewMode: 'empty',
    previewPath: '',
    imageTexture: null,
    imageInfo: null,
    imageError: '',
    audioBuffer: null,
    audioPlayback: null,
    audioLoop: false,
    audioError: '',
    selectedSize: 0,
    touchScrollTarget: null,
    lastTouchY: 0,
    pointerResizeId: null,
    lastPinchDist: 0,
    pinching: false,
};

let imageShader = null;
let audioPlayPath = null;

function resetDirectoryView(path, entries) {
    setFilterActive(false);
    state.currentPath = path;
    state.allEntries = sortEntries(entries);
    state.filterQuery = '';
    state.filterSelectionStart = 0;
    state.filterSelectionEnd = 0;
    applyFilter();
    state.selectedIndex = -1;
    state.scrollY = 0;
    state.errorMessage = '';
}

function setFilterActive(active) {
    if (state.filterActive === active) return;
    state.filterActive = active;
    if (active) {
        state.filterSelectionStart = state.filterQuery.length;
        state.filterSelectionEnd = state.filterQuery.length;
        sys.input.startTextInput({
            text: state.filterQuery,
            selectionStart: state.filterSelectionStart,
            selectionEnd: state.filterSelectionEnd,
            multiline: false,
        });
    } else {
        sys.input.stopTextInput();
    }
}

function replaceFilterSelection(text) {
    const start = Math.min(state.filterSelectionStart, state.filterSelectionEnd);
    const end = Math.max(state.filterSelectionStart, state.filterSelectionEnd);
    state.filterQuery = state.filterQuery.slice(0, start) + text + state.filterQuery.slice(end);
    const caret = start + text.length;
    state.filterSelectionStart = caret;
    state.filterSelectionEnd = caret;
    applyFilter();
    state.filterActive = true;
}

function applyFilter() {
    const query = state.filterQuery.trim().toLocaleLowerCase();
    state.entries = query
        ? state.allEntries.filter((entry) => entry.name.toLocaleLowerCase().includes(query))
        : state.allEntries.slice();
    state.selectedIndex = -1;
    state.hoveredIndex = -1;
    state.scrollY = 0;
    clearPreview();
    reconcileRowVisuals();
}

function getEntryKey(entry) {
    return state.currentPath + '\0' + entry.type + '\0' + entry.name;
}

function reconcileRowVisuals() {
    const desiredKeys = new Set();
    const rowHeight = ROW_HEIGHT || BASE_ROW_HEIGHT * fontScale;

    for (let index = 0; index < state.entries.length; index++) {
        const entry = state.entries[index];
        const key = getEntryKey(entry);
        desiredKeys.add(key);
        let visual = state.rowVisuals.get(key);
        if (!visual) {
            visual = {
                key,
                entry,
                index,
                y: index * rowHeight + Math.min(24, rowHeight * 0.35),
                velocity: 0,
                opacity: 0,
                scale: 0.96,
                exiting: false,
            };
            state.rowVisuals.set(key, visual);
        }
        visual.entry = entry;
        visual.index = index;
        visual.exiting = false;
    }

    for (const visual of state.rowVisuals.values()) {
        if (!desiredKeys.has(visual.key)) visual.exiting = true;
    }
}

function updateRowVisuals(deltaTime) {
    const dt = Math.min(0.05, Math.max(0, deltaTime || 0));
    const positionDamping = Math.exp(-18 * dt);
    const fadeBlend = 1 - Math.exp(-16 * dt);

    for (const [key, visual] of state.rowVisuals) {
        const targetY = visual.exiting ? visual.y - ROW_HEIGHT * 0.15 : visual.index * ROW_HEIGHT;
        const targetOpacity = visual.exiting ? 0 : 1;
        const targetScale = visual.exiting ? 0.96 : 1;
        visual.velocity = (visual.velocity + (targetY - visual.y) * 170 * dt) * positionDamping;
        visual.y += visual.velocity * dt;
        visual.opacity += (targetOpacity - visual.opacity) * fadeBlend;
        visual.scale += (targetScale - visual.scale) * fadeBlend;

        if (visual.exiting && visual.opacity < 0.01) state.rowVisuals.delete(key);
    }
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

        resetDirectoryView(path, entries);
    } catch (e) {
        state.errorMessage = 'Error listing: ' + (e.message || e);
    }
}

function goBack() {
    if (state.historyIndex > 0) {
        const nextIndex = state.historyIndex - 1;
        const path = state.history[nextIndex];
        try {
            resetDirectoryView(path, loadDirectory(path));
            state.historyIndex = nextIndex;
        } catch (e) {
            state.errorMessage = 'Error: ' + (e.message || e);
        }
    }
}

function goForward() {
    if (state.historyIndex < state.history.length - 1) {
        const nextIndex = state.historyIndex + 1;
        const path = state.history[nextIndex];
        try {
            resetDirectoryView(path, loadDirectory(path));
            state.historyIndex = nextIndex;
        } catch (e) {
            state.errorMessage = 'Error: ' + (e.message || e);
        }
    }
}

function goHome() {
    navigateTo('');
}

function goUp() {
    const parts = state.currentPath.split('/').filter(Boolean);
    if (parts.length > 0) {
        parts.pop();
        navigateTo(parts.join('/'));
    } else if (state.currentPath !== '') {
        navigateTo('');
    }
}

function sortEntries(entries) {
    const dirs = entries
        .filter((entry) => entry.type === 'directory')
        .sort((a, b) => a.name.localeCompare(b.name));
    const files = entries
        .filter((entry) => entry.type === 'file')
        .sort((a, b) => a.name.localeCompare(b.name));

    return [...dirs, ...files];
}

function getEntryPath(entry) {
    return state.currentPath ? state.currentPath + '/' + entry.name : entry.name;
}

function selectEntry(index) {
    if (index < 0 || index >= state.entries.length) return;
    const entry = state.entries[index];
    state.selectedIndex = index;

    if (entry.type === 'directory') {
        navigateTo(getEntryPath(entry));
    } else {
        previewFile(entry);
    }
}

function previewSelection() {
    if (state.selectedIndex < 0 || state.selectedIndex >= state.entries.length) {
        clearPreview();
        return;
    }

    const entry = state.entries[state.selectedIndex];
    if (entry.type === 'file') {
        previewFile(entry);
    } else {
        clearPreview();
    }
}

function moveSelection(delta, listHeight) {
    if (state.entries.length === 0) return;
    const nextIndex = state.selectedIndex < 0
        ? (delta > 0 ? 0 : state.entries.length - 1)
        : clamp(state.selectedIndex + delta, 0, state.entries.length - 1);

    if (nextIndex === state.selectedIndex) return;
    state.selectedIndex = nextIndex;
    previewSelection();
    ensureVisible(listHeight);
}

function clearPreview() {
    stopAudioPreview();
    if (state.imageTexture !== null) {
        try { sys.gl.destroyTexture(state.imageTexture); } catch (e) { }
    }
    state.previewText = null;
    state.previewName = '';
    state.previewPath = '';
    state.previewMode = 'empty';
    state.previewScroll = 0;
    state.imageTexture = null;
    state.imageInfo = null;
    state.imageError = '';
    state.audioBuffer = null;
    state.audioPlayback = null;
    state.audioError = '';
    state.selectedSize = 0;
}

function stopAudioPreview() {
    if (state.audioPlayback !== null) {
        try { sys.audio.stopBuffer(state.audioPlayback); } catch (e) { }
    }
    if (state.audioBuffer !== null) {
        try { sys.audio.destroyBuffer(state.audioBuffer); } catch (e) { }
    }
    state.audioPlayback = null;
    state.audioBuffer = null;
}

function previewFile(entry) {
    const path = getEntryPath(entry);
    clearPreview();
    state.previewName = entry.name;
    state.previewPath = path;
    state.previewScroll = 0;
    state.selectedSize = entry.size !== undefined ? entry.size : 0;

    const previewLimit = isTextFile(entry.name) ? MAX_TEXT_PREVIEW_BYTES
        : (isImageFile(entry.name) || isAudioFile(entry.name) ? MAX_MEDIA_PREVIEW_BYTES : 0);
    if (previewLimit > 0 && state.selectedSize > previewLimit) {
        state.previewMode = 'binary';
        state.previewText = '[Preview unavailable]\n\n' + formatSize(state.selectedSize) +
            ' exceeds the ' + formatSize(previewLimit) + ' preview limit.';
        return;
    }

    if (isImageFile(entry.name)) {
        previewImage(path, entry);
        return;
    }

    if (isAudioFile(entry.name)) {
        previewAudio(path, entry);
        return;
    }

    if (!isTextFile(entry.name)) {
        const size = entry.size !== undefined ? entry.size : 0;
        state.previewMode = 'binary';
        state.previewText = '[Binary file — ' + formatSize(size) + ']';
        return;
    }

    try {
        state.previewMode = 'text';
        const text = sys.files.readText(path);
        const lines = text.split('\n');
        if (lines.length > MAX_TEXT_PREVIEW_LINES) {
            state.previewText = lines.slice(0, MAX_TEXT_PREVIEW_LINES).join('\n') + '\n\n... (' + lines.length + ' lines total)';
        } else {
            state.previewText = text;
        }
    } catch (e) {
        state.previewMode = 'text';
        state.previewText = '[Cannot read file: ' + (e.message || e) + ']';
    }
}

function previewImage(path, entry) {
    state.previewMode = 'image';
    state.previewText = null;

    try {
        const imageBytes = sys.files.readBinary(path);
        state.imageInfo = readImageInfo(path, imageBytes);
        // Decode the browsed file bytes directly instead of resolving a project asset path.
        state.imageTexture = sys.gl.loadTexture2DFromBuffer(imageBytes);
    } catch (e) {
        state.imageError = (e.message || e) + '';
        state.previewText = '[Image file — ' + formatSize(entry.size || 0) + ']\n\n' +
            'The File API can inspect it, but the image bytes could not be decoded into a GL texture.\n\n' +
            'Decoder message: ' + state.imageError;
    }
}

function previewAudio(path, entry) {
    state.previewMode = 'audio';
    state.previewText = null;

    try {
        sys.audio.start();
        const audioBytes = sys.files.readBinary(path);
        // Audio buffers decode WAV/MP3/Ogg/FLAC bytes into playable PCM owned by sys.audio.
        state.audioBuffer = sys.audio.loadBufferFromBuffer(audioBytes);
        if (state.audioBuffer < 0) {
            state.audioBuffer = null;
            state.audioError = sys.audio.getError() || 'Could not decode audio';
        }
    } catch (e) {
        state.audioError = (e.message || e) + '';
    }

    if (state.audioError) {
        state.previewText = '[Audio file — ' + formatSize(entry.size || 0) + ']\n\n' +
            'Budo can decode WAV, MP3, Ogg Vorbis, and FLAC through sys.files.readBinary() ' +
            'and sys.audio.loadBufferFromBuffer().\n\n' +
            'Decoder message: ' + state.audioError;
    }
}

function isTextFile(name) {
    const ext = getExtension(name);

    return TEXT_EXTENSIONS.indexOf(ext) >= 0 || ext === '';
}

function isImageFile(name) {
    return IMAGE_EXTENSIONS.indexOf(getExtension(name)) >= 0;
}

function isAudioFile(name) {
    return AUDIO_EXTENSIONS.indexOf(getExtension(name)) >= 0;
}

function getExtension(name) {
    return name.lastIndexOf('.') >= 0 ? name.substring(name.lastIndexOf('.')).toLowerCase() : '';
}

function readImageInfo(path, imageBytes) {
    try {
        // Header parsing keeps metadata available even when GL cannot decode the image.
        const bytes = new Uint8Array(imageBytes);
        if (bytes.length >= 24 && bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4E && bytes[3] === 0x47) {
            return {
                type: 'PNG',
                width: readU32BE(bytes, 16),
                height: readU32BE(bytes, 20),
            };
        }
        if (bytes.length >= 10 && bytes[0] === 0x47 && bytes[1] === 0x49 && bytes[2] === 0x46) {
            return {
                type: 'GIF',
                width: readU16LE(bytes, 6),
                height: readU16LE(bytes, 8),
            };
        }
        if (bytes.length >= 26 && bytes[0] === 0x42 && bytes[1] === 0x4D) {
            return {
                type: 'BMP',
                width: readU32LE(bytes, 18),
                height: Math.abs(readI32LE(bytes, 22)),
            };
        }
        if (bytes.length >= 18 && (getExtension(path) === '.tga')) {
            return {
                type: 'TGA',
                width: readU16LE(bytes, 12),
                height: readU16LE(bytes, 14),
            };
        }
        const jpg = readJpegInfo(bytes);
        if (jpg) return jpg;
    } catch (e) { }
    return null;
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
        if (marker >= 0xC0 && marker <= 0xCF && marker !== 0xC4 && marker !== 0xC8 && marker !== 0xCC) {
            return {
                type: 'JPEG',
                width: readU16BE(bytes, i + 7),
                height: readU16BE(bytes, i + 5),
            };
        }
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

function sz(base) {
    return base * fontScale;
}

function drawFolderIcon(x, y) {
    sys.canvas.setFillColor(COLORS.folder);
    const s = ICON_SIZE / 16;
    sys.canvas.drawRect(x, y + 2 * s, 7 * s, 3 * s);
    sys.canvas.drawRect(x, y + 4 * s, ICON_SIZE, ICON_SIZE - 6 * s);
}

function drawFileIcon(x, y) {
    const s = ICON_SIZE / 16;
    sys.canvas.setStrokeColor(COLORS.file);
    sys.canvas.setStrokeWidth(1.5 * s);
    sys.canvas.drawRect(x + 2 * s, y, ICON_SIZE - 4 * s, ICON_SIZE);
    sys.canvas.setFillColor(COLORS.file);
    sys.canvas.drawRect(x + 5 * s, y + 4 * s, ICON_SIZE - 10 * s, 1.5 * s);
    sys.canvas.drawRect(x + 5 * s, y + 7 * s, ICON_SIZE - 10 * s, 1.5 * s);
    sys.canvas.drawRect(x + 5 * s, y + 10 * s, ICON_SIZE - 10 * s, 1.5 * s);
}

function drawImageIcon(x, y) {
    const s = ICON_SIZE / 16;
    sys.canvas.setStrokeColor(COLORS.accent);
    sys.canvas.setStrokeWidth(1.5 * s);
    sys.canvas.drawRect(x + 1 * s, y + 2 * s, ICON_SIZE - 2 * s, ICON_SIZE - 4 * s);
    sys.canvas.setFillColor(COLORS.warning);
    sys.canvas.drawCircle(x + 5 * s, y + 6 * s, 2 * s);
    sys.canvas.setFillColor(COLORS.accent);
    sys.canvas.drawRect(x + 4 * s, y + 11 * s, 4 * s, 2 * s);
    sys.canvas.drawRect(x + 8 * s, y + 9 * s, 5 * s, 4 * s);
}

function drawAudioIcon(x, y) {
    const s = ICON_SIZE / 16;
    sys.canvas.setFillColor(COLORS.success);
    sys.canvas.drawRect(x + 2 * s, y + 6 * s, 4 * s, 5 * s);
    sys.canvas.drawRect(x + 5 * s, y + 4 * s, 3 * s, 9 * s);
    sys.canvas.setStrokeColor(COLORS.success);
    sys.canvas.setStrokeWidth(1.5 * s);
    sys.canvas.drawArc(x + 7 * s, y + 4 * s, 7 * s, 8 * s, -45, 90, false);
}

function truncateText(text, maxChars) {
    if (text.length <= maxChars) return text;
    return text.substring(0, maxChars - 3) + '...';
}

function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
}

function getSidebarWidth(width) {
    const minW = Math.min(BASE_SIDEBAR_MIN_WIDTH, width * 0.65);
    const maxW = Math.max(minW, width - Math.max(PREVIEW_MIN_WIDTH, width * 0.25));
    return Math.round(clamp(width * state.sidebarRatio, minW, maxW));
}

function setSidebarWidth(width, sidebarWidth) {
    const minW = Math.min(BASE_SIDEBAR_MIN_WIDTH, width * 0.65);
    const maxW = Math.max(minW, width - Math.max(PREVIEW_MIN_WIDTH, width * 0.25));
    const clampedW = clamp(sidebarWidth, minW, maxW);
    state.sidebarRatio = clampedW / width;
}

function isOnDivider(x, y, sidebarW) {
    return y > HEADER_HEIGHT && Math.abs(x - sidebarW) <= DIVIDER_HIT_WIDTH / 2;
}

function getSidebarMetrics(sidebarW, height) {
    const navY = HEADER_HEIGHT + 4;
    const listStartY = navY + Math.round(32 * fontScale);
    const colHeaderEnd = listStartY + Math.round(26 * fontScale);

    return {
        navY,
        listStartY,
        colHeaderEnd,
        listY: colHeaderEnd,
        listHeight: height - colHeaderEnd,
        sidebarW,
    };
}

function getNavButtonWidth(sidebarW) {
    return Math.max(1, Math.min(sz(40), (sidebarW - PADDING * 2) / NAV_BUTTON_COUNT));
}

function ensureImageShader() {
    if (imageShader === null) {
        imageShader = new ShaderProgram(sys.gl.createProgram('image_preview.vert', 'image_preview.frag'));
    }
    return imageShader;
}

function textWidth(text, fontSize) {
    const rect = sys.canvas.measureTextRect ? sys.canvas.measureTextRect(text, fontSize) : null;
    return rect && rect.width ? rect.width : text.length * fontSize * 0.55;
}

function render(width, height) {
    sys.canvas.clear(COLORS.bg);

    const sidebarW = getSidebarWidth(width);

    drawHeader(width);
    drawSidebar(sidebarW, height);
    drawDivider(sidebarW, height);
    drawPreview(sidebarW, width, height);
}

function drawDivider(sidebarW, height) {
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawLine(sidebarW, HEADER_HEIGHT, sidebarW, height);
    sys.canvas.setFillColor(state.resizingSidebar || state.hoveringDivider ? COLORS.accent : COLORS.divider);
    sys.canvas.drawRect(sidebarW - DIVIDER_WIDTH / 2, HEADER_HEIGHT, DIVIDER_WIDTH, height - HEADER_HEIGHT);
}

function drawHeader(width) {
    // Header background
    sys.canvas.setFillColor(COLORS.header);
    sys.canvas.drawRect(0, 0, width, HEADER_HEIGHT);

    // Breadcrumb path
    const displayPath = state.currentPath || '/';
    const headerTextY = HEADER_HEIGHT * 0.625;
    sys.canvas.setFillColor(COLORS.accent);
    sys.canvas.drawText('File Explorer', PADDING, headerTextY, sz(16));

    // Entry count on the right
    const countText = state.filterQuery
        ? state.entries.length + ' of ' + state.allEntries.length + ' items'
        : state.entries.length + ' items';
    const countWidth = textWidth(countText, sz(12));
    sys.canvas.setFillColor(COLORS.textMuted);
    sys.canvas.drawText(countText, width - countWidth - PADDING, headerTextY, sz(12));

    const pathX = PADDING + sz(125);
    const pathWidth = Math.max(0, width - pathX - countWidth - PADDING * 2);
    if (pathWidth > sz(24)) {
        sys.canvas.save();
        sys.canvas.clipRect(pathX, 0, pathWidth, HEADER_HEIGHT);
        sys.canvas.setFillColor(COLORS.headerText);
        sys.canvas.drawText('/ ' + displayPath, pathX, headerTextY, sz(14));
        sys.canvas.restore();
    }

    // Error message
    if (state.errorMessage) {
        sys.canvas.setFillColor(COLORS.error);
        sys.canvas.drawText(truncateText(state.errorMessage, 40), pathX, HEADER_HEIGHT - sz(4), sz(10));
    }

    // Bottom border
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawLine(0, HEADER_HEIGHT, width, HEADER_HEIGHT);
}

function drawSidebar(sidebarW, height) {
    const layout = getSidebarMetrics(sidebarW, height);

    // Sidebar background
    sys.canvas.setFillColor(COLORS.sidebar);
    sys.canvas.drawRect(0, HEADER_HEIGHT, sidebarW, height - HEADER_HEIGHT);

    // Navigation row
    const navButtonW = getNavButtonWidth(sidebarW);
    drawNavButton('<', PADDING, layout.navY, navButtonW, state.historyIndex > 0);
    drawNavButton('>', PADDING + navButtonW, layout.navY, navButtonW, state.historyIndex < state.history.length - 1);
    drawNavButton('^', PADDING + navButtonW * 2, layout.navY, navButtonW, state.currentPath !== '');
    drawNavButton('/', PADDING + navButtonW * 3, layout.navY, navButtonW, state.currentPath !== '');
    drawRefreshButton(PADDING + navButtonW * 4, layout.navY, navButtonW);

    // Column header
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawLine(0, layout.listStartY, sidebarW, layout.listStartY);

    sys.canvas.setFillColor(state.filterActive ? COLORS.accent : COLORS.textMuted);
    const filterLabel = state.filterQuery || state.filterActive ? 'Filter: ' + state.filterQuery + (state.filterActive ? '|' : '') : 'Name';
    sys.canvas.drawText(truncateText(filterLabel, Math.max(8, Math.floor((sidebarW - sz(115)) / sz(7)))), PADDING + ICON_SIZE + 8, layout.listStartY + sz(18), sz(11));
    sys.canvas.drawText('Size', sidebarW - sz(75), layout.listStartY + sz(18), sz(11));

    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.drawLine(0, layout.colHeaderEnd, sidebarW, layout.colHeaderEnd);

    if (state.entries.length === 0) {
        sys.canvas.setFillColor(COLORS.textMuted);
        const emptyText = state.filterQuery ? 'No matching files' : 'This folder is empty';
        sys.canvas.drawText(emptyText, PADDING, layout.colHeaderEnd + sz(24), sz(12));
    }

    // Clip file list area
    sys.canvas.save();
    sys.canvas.clipRect(0, layout.listY, sidebarW, layout.listHeight);

    // File list
    const maxScroll = Math.max(0, state.entries.length * ROW_HEIGHT - layout.listHeight);
    state.scrollY = Math.max(0, Math.min(state.scrollY, maxScroll));

    for (const visual of state.rowVisuals.values()) {
        const entry = visual.entry;
        const rowY = layout.listY + visual.y - state.scrollY;
        if (rowY + ROW_HEIGHT < layout.listY || rowY > layout.listY + layout.listHeight) continue;
        const alpha = Math.round(255 * clamp(visual.opacity, 0, 1));
        sys.canvas.setAlpha(alpha);
        sys.canvas.save();
        sys.canvas.translate(0, rowY + ROW_HEIGHT / 2);
        sys.canvas.scale(1, visual.scale);
        sys.canvas.translate(0, -(rowY + ROW_HEIGHT / 2));

        // Hover/selected highlight
        if (!visual.exiting && visual.index === state.selectedIndex) {
            sys.canvas.setFillColor(COLORS.selected);
            sys.canvas.drawRect(0, rowY, sidebarW, ROW_HEIGHT);
        } else if (!visual.exiting && visual.index === state.hoveredIndex) {
            sys.canvas.setFillColor(COLORS.hover);
            sys.canvas.drawRect(0, rowY, sidebarW, ROW_HEIGHT);
        }

        // Icon
        const iconY = rowY + (ROW_HEIGHT - ICON_SIZE) / 2;
        if (entry.type === 'directory') {
            drawFolderIcon(PADDING, iconY);
        } else if (isImageFile(entry.name)) {
            drawImageIcon(PADDING, iconY);
        } else if (isAudioFile(entry.name)) {
            drawAudioIcon(PADDING, iconY);
        } else {
            drawFileIcon(PADDING, iconY);
        }

        // Name
        const nameColor = entry.type === 'directory' ? COLORS.accent : (isAudioFile(entry.name) ? COLORS.success : COLORS.text);
        sys.canvas.setFillColor(nameColor);
        const maxNameChars = Math.floor((sidebarW - sz(120)) / sz(7));
        sys.canvas.drawText(truncateText(entry.name, maxNameChars), PADDING + ICON_SIZE + 8, rowY + ROW_HEIGHT * 0.66, sz(13));

        // Size (files only)
        if (entry.type === 'file' && entry.size !== undefined) {
            sys.canvas.setFillColor(COLORS.textMuted);
            sys.canvas.drawText(formatSize(entry.size), sidebarW - sz(75), rowY + ROW_HEIGHT * 0.66, sz(11));
        } else if (entry.type === 'directory') {
            sys.canvas.setFillColor(COLORS.textMuted);
            sys.canvas.drawText('dir', sidebarW - sz(75), rowY + ROW_HEIGHT * 0.66, sz(11));
        }

        sys.canvas.restore();
        sys.canvas.setAlpha(255);
    }

    // Scrollbar
    if (state.entries.length * ROW_HEIGHT > layout.listHeight) {
        const scrollRatio = state.scrollY / maxScroll;
        const thumbHeight = Math.max(20, (layout.listHeight / (state.entries.length * ROW_HEIGHT)) * layout.listHeight);
        const thumbY = layout.listY + scrollRatio * (layout.listHeight - thumbHeight);

        sys.canvas.setFillColor(COLORS.scrollbar);
        sys.canvas.drawRoundRect(sidebarW - SCROLLBAR_WIDTH - 2, thumbY, SCROLLBAR_WIDTH, thumbHeight, 4, 4);
    }

    sys.canvas.restore();
}

function drawNavButton(label, x, y, width, enabled) {
    sys.canvas.setFillColor(enabled ? COLORS.text : COLORS.textMuted);
    sys.canvas.drawText(label, x + (width - textWidth(label, sz(14))) / 2, y + sz(20), sz(14));
}

function drawRefreshButton(x, y, width) {
    const size = Math.min(sz(14), width * 0.45);
    const iconX = x + (width - size) / 2;
    const iconY = y + sz(6);
    sys.canvas.setStrokeColor(COLORS.text);
    sys.canvas.setStrokeWidth(Math.max(1, sz(1)));
    sys.canvas.drawArc(iconX, iconY, size, size, -65, 285, false);
    sys.canvas.drawLine(iconX + size * 0.72, iconY, iconX + size, iconY + size * 0.05);
    sys.canvas.drawLine(iconX + size, iconY + size * 0.05, iconX + size * 0.9, iconY + size * 0.3);
}

function drawPreview(sidebarW, width, height) {
    const previewX = sidebarW + DIVIDER_WIDTH;
    const previewW = width - previewX;
    const previewY = HEADER_HEIGHT;
    const previewH = height - HEADER_HEIGHT;

    if (state.previewMode === 'empty') {
        // No file selected
        sys.canvas.setFillColor(COLORS.textMuted);
        const hintX = previewX + previewW / 2 - sz(100);
        const hintY = previewY + previewH / 2;
        sys.canvas.drawText('Select a file to preview', hintX, hintY, sz(16));
        return;
    }

    // Preview header
    const previewHeaderH = Math.round(36 * fontScale);
    sys.canvas.setFillColor(COLORS.header);
    sys.canvas.drawRect(previewX, previewY, previewW, previewHeaderH);
    sys.canvas.setFillColor(COLORS.accent);
    sys.canvas.drawText(state.previewName, previewX + PADDING, previewY + previewHeaderH * 0.67, sz(13));
    sys.canvas.setFillColor(COLORS.textMuted);
    const meta = getPreviewMeta();
    if (meta) {
        sys.canvas.drawText(meta, previewX + previewW - textWidth(meta, sz(11)) - PADDING, previewY + previewHeaderH * 0.67, sz(11));
    }
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawLine(previewX, previewY + previewHeaderH, previewX + previewW, previewY + previewHeaderH);

    // Preview body
    const bodyY = previewY + previewHeaderH;
    const bodyH = previewH - previewHeaderH;
    sys.canvas.setFillColor(COLORS.previewBg);
    sys.canvas.drawRect(previewX, bodyY, previewW, bodyH);

    if (state.previewMode === 'image' && state.imageTexture !== null) {
        drawImagePreview(previewX, bodyY, previewW, bodyH);
        return;
    }

    if (state.previewMode === 'audio' && state.audioBuffer !== null) {
        drawAudioPreview(previewX, bodyY, previewW, bodyH);
        return;
    }

    if (state.previewText === null) return;

    sys.canvas.save();
    sys.canvas.clipRect(previewX, bodyY, previewW, bodyH);

    const lines = state.previewText.split('\n');
    const lineHeight = sz(16);
    const maxPreviewScroll = Math.max(0, lines.length * lineHeight - bodyH + PADDING * 2);
    state.previewScroll = Math.max(0, Math.min(state.previewScroll, maxPreviewScroll));

    const startLine = Math.floor(state.previewScroll / lineHeight);
    const maxVisibleLines = Math.ceil(bodyH / lineHeight) + 1;
    const endLine = Math.min(lines.length, startLine + maxVisibleLines);

    // Line numbers and content
    const lineNumW = Math.max(sz(40), textWidth(String(lines.length), sz(10)) + sz(14));
    const glyphW = Math.max(1, textWidth('M', sz(12)));
    for (let i = startLine; i < endLine; i++) {
        const y = bodyY + PADDING + i * lineHeight - state.previewScroll;

        // Line number
        sys.canvas.setFillColor(COLORS.textMuted);
        sys.canvas.drawText(String(i + 1), previewX + 6, y + sz(12), sz(10));

        // Text content
        sys.canvas.setFillColor(COLORS.textPreview);
        const maxLineChars = Math.max(4, Math.floor((previewW - lineNumW - PADDING * 2) / glyphW));
        sys.canvas.drawText(truncateText(lines[i], maxLineChars), previewX + lineNumW, y + sz(12), sz(12));
    }

    sys.canvas.restore();
}

function getPreviewMeta() {
    if (state.previewMode === 'image' && state.imageInfo) {
        return state.imageInfo.type + '  ' + state.imageInfo.width + 'x' + state.imageInfo.height + '  ' + formatSize(state.selectedSize);
    }
    if (state.previewMode === 'audio') {
        return getExtension(state.previewName).substring(1).toUpperCase() + '  ' + formatSize(state.selectedSize);
    }
    if (state.selectedSize > 0) return formatSize(state.selectedSize);
    return '';
}

function drawImagePreview(previewX, bodyY, previewW, bodyH) {
    const pad = PADDING * 2;
    const info = state.imageInfo || { width: 1, height: 1 };
    const availableW = Math.max(1, previewW - pad * 2);
    const availableH = Math.max(1, bodyH - pad * 2);
    const scale = Math.min(availableW / info.width, availableH / info.height);
    const drawW = Math.max(1, Math.round(info.width * scale));
    const drawH = Math.max(1, Math.round(info.height * scale));
    const x = Math.round(previewX + (previewW - drawW) / 2);
    const y = Math.round(bodyY + (bodyH - drawH) / 2);

    sys.canvas.setFillColor('#000000');
    sys.canvas.drawRect(x - 1, y - 1, drawW + 2, drawH + 2);
    sys.gl.bindScreen();
    ensureImageShader()
        .use()
        .texture('u_image', state.imageTexture, 1)
        .drawRegion(x, y, drawW, drawH, -1);
}

function drawAudioPreview(previewX, bodyY, previewW, bodyH) {
    const centerX = previewX + previewW / 2;
    const centerY = bodyY + bodyH / 2;
    const playing = state.audioPlayback !== null;
    const radius = Math.min(previewW, bodyH) * 0.16;

    sys.canvas.setFillColor(playing ? COLORS.success : COLORS.accent);
    sys.canvas.drawCircle(centerX, centerY - sz(18), radius);
    sys.canvas.setFillColor(COLORS.previewBg);
    if (playing) {
        sys.canvas.drawRect(centerX - radius * 0.28, centerY - sz(18) - radius * 0.42, radius * 0.18, radius * 0.84);
        sys.canvas.drawRect(centerX + radius * 0.10, centerY - sz(18) - radius * 0.42, radius * 0.18, radius * 0.84);
    } else {
        if (audioPlayPath === null) audioPlayPath = sys.path.create();
        sys.path.reset(audioPlayPath);
        sys.path.moveTo(audioPlayPath, centerX - radius * 0.22, centerY - sz(18) - radius * 0.38);
        sys.path.lineTo(audioPlayPath, centerX - radius * 0.22, centerY - sz(18) + radius * 0.38);
        sys.path.lineTo(audioPlayPath, centerX + radius * 0.38, centerY - sz(18));
        sys.path.close(audioPlayPath);
        sys.canvas.drawPath(audioPlayPath);
    }

    sys.canvas.setFillColor(COLORS.textPreview);
    const title = playing ? 'Playing audio buffer' : 'Click to play audio buffer';
    sys.canvas.drawText(title, centerX - textWidth(title, sz(16)) / 2, centerY + radius + sz(22), sz(16));
    sys.canvas.setFillColor(COLORS.textMuted);
    const hint = 'Space toggles play/stop, L toggles loop';
    sys.canvas.drawText(hint, centerX - textWidth(hint, sz(12)) / 2, centerY + radius + sz(46), sz(12));
    const loop = state.audioLoop ? 'Loop on' : 'Loop off';
    sys.canvas.drawText(loop, centerX - textWidth(loop, sz(11)) / 2, centerY + radius + sz(66), sz(11));
}

function handleInput(input, width, height) {
    const sidebarW = getSidebarWidth(width);
    const layout = getSidebarMetrics(sidebarW, height);
    const mx = input.mouse.x;
    const my = input.mouse.y;

    handleTouchScroll(input, sidebarW, layout, width);
    state.hoveringDivider = isOnDivider(mx, my, sidebarW);

    if (state.pointerResizeId !== null) {
        return;
    }

    if (input.mouse.leftPressed && state.hoveringDivider) {
        state.resizingSidebar = true;
    }
    if (state.resizingSidebar) {
        if (input.mouse.left) {
            setSidebarWidth(width, mx);
        } else {
            state.resizingSidebar = false;
        }
        return;
    }

    // Mouse hover in file list
    state.hoveredIndex = -1;
    if (mx >= 0 && mx < sidebarW && my > layout.colHeaderEnd) {
        const relY = my - layout.colHeaderEnd + state.scrollY;
        const idx = Math.floor(relY / ROW_HEIGHT);
        if (idx >= 0 && idx < state.entries.length) {
            state.hoveredIndex = idx;
        }
    }

    // Mouse clicks
    if (input.mouse.leftPressed) {
        if (state.previewMode === 'audio' && state.audioBuffer !== null && mx >= sidebarW && my > HEADER_HEIGHT) {
            toggleAudioPlayback();
            return;
        }
        if (my >= layout.navY && my < layout.listStartY) {
            const navIndex = Math.floor((mx - PADDING) / getNavButtonWidth(sidebarW));
            if (navIndex === 0) goBack();
            if (navIndex === 1) goForward();
            if (navIndex === 2) goUp();
            if (navIndex === 3) goHome();
            if (navIndex === 4) navigateTo(state.currentPath);
            if (navIndex >= 0 && navIndex < NAV_BUTTON_COUNT) return;
        }
        if (mx < sidebarW && my >= layout.listStartY && my < layout.colHeaderEnd) {
            setFilterActive(true);
            return;
        }
        // File list click
        if (state.hoveredIndex >= 0) {
            setFilterActive(false);
            selectEntry(state.hoveredIndex);
            return;
        }
    }

    // Scroll with mouse wheel
    if (mx < sidebarW && my > layout.colHeaderEnd) {
        state.scrollY -= input.mouse.wheelY * ROW_HEIGHT;
    } else if (mx >= sidebarW && my > HEADER_HEIGHT) {
        state.previewScroll -= input.mouse.wheelY * sz(16);
    }

    if (state.filterActive) {
        if (input.textEdit) {
            state.filterQuery = input.textEdit.text;
            state.filterSelectionStart = input.textEdit.selectionStart;
            state.filterSelectionEnd = input.textEdit.selectionEnd;
            applyFilter();
            state.filterActive = true;
        } else if (input.text) {
            replaceFilterSelection(input.text);
        }
        if (!input.textEdit && sys.input.isKeyPressed(SCANCODE.BACKSPACE)) {
            if (state.filterSelectionStart !== state.filterSelectionEnd) {
                replaceFilterSelection('');
            } else if (state.filterSelectionStart > 0) {
                state.filterSelectionStart--;
                replaceFilterSelection('');
            }
        }

        const caretX = PADDING + ICON_SIZE + 8 + textWidth('Filter: ' + state.filterQuery.slice(0, state.filterSelectionEnd), sz(11));
        sys.input.updateTextInput({
            text: state.filterQuery,
            selectionStart: state.filterSelectionStart,
            selectionEnd: state.filterSelectionEnd,
            caret: { x: caretX, y: layout.listStartY, width: 1, height: layout.colHeaderEnd - layout.listStartY },
        });
    }

    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.EQUALS)) {
        fontScale = Math.min(FONT_SCALE_MAX, fontScale + FONT_SCALE_STEP);
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.MINUS)) {
        fontScale = Math.max(FONT_SCALE_MIN, fontScale - FONT_SCALE_STEP);
    }

    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.SPACE) && state.previewMode === 'audio' && state.audioBuffer !== null) {
        toggleAudioPlayback();
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.L) && state.previewMode === 'audio') {
        state.audioLoop = !state.audioLoop;
        if (state.audioPlayback !== null) {
            toggleAudioPlayback();
            toggleAudioPlayback();
        }
    }

    // Touch pinch-to-zoom
    if (input.pointers && input.pointers.length >= 2) {
        const dx = input.pointers[1].x - input.pointers[0].x;
        const dy = input.pointers[1].y - input.pointers[0].y;
        const dist = Math.sqrt(dx * dx + dy * dy);
        if (state.pinching && state.lastPinchDist > 0) {
            const ratio = dist / state.lastPinchDist;
            fontScale = Math.max(FONT_SCALE_MIN, Math.min(FONT_SCALE_MAX, fontScale * ratio));
            // Snap to nearest step for cleanliness
            fontScale = Math.round(fontScale / FONT_SCALE_STEP) * FONT_SCALE_STEP;
        }
        state.lastPinchDist = dist;
        state.pinching = true;
    } else {
        state.pinching = false;
        state.lastPinchDist = 0;
    }

    // Keyboard navigation
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.R)) {
        navigateTo(state.currentPath);
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.H)) {
        goHome();
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.F)) {
        setFilterActive(true);
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.LEFT)) {
        goBack();
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.RIGHT)) {
        if (state.selectedIndex >= 0) {
            selectEntry(state.selectedIndex);
        }
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.UP)) {
        moveSelection(-1, layout.listHeight);
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.DOWN)) {
        moveSelection(1, layout.listHeight);
    }
    if (sys.input.isKeyPressed(SCANCODE.ESCAPE)) {
        if (state.filterActive || state.filterQuery) {
            state.filterQuery = '';
            setFilterActive(false);
            applyFilter();
        } else {
            goUp();
        }
    }
    if (!state.filterActive && sys.input.isKeyPressed(SCANCODE.RETURN)) {
        if (state.selectedIndex >= 0) {
            selectEntry(state.selectedIndex);
        }
    }
}

function handleTouchScroll(input, sidebarW, layout, width) {
    if (!input.pointers || input.pointers.length !== 1) {
        state.touchScrollTarget = null;
        state.pointerResizeId = null;
        if (!input.mouse.left) state.resizingSidebar = false;
        return;
    }

    const pointer = input.pointers[0];
    if (!pointer.down) {
        state.touchScrollTarget = null;
        state.pointerResizeId = null;
        if (!input.mouse.left) state.resizingSidebar = false;
        return;
    }

    if (pointer.pressed) {
        if (isOnDivider(pointer.x, pointer.y, sidebarW)) {
            state.pointerResizeId = 0;
            state.resizingSidebar = true;
            state.touchScrollTarget = null;
        } else if (pointer.x < sidebarW && pointer.y > layout.colHeaderEnd) {
            state.touchScrollTarget = 'sidebar';
        } else if (pointer.x >= sidebarW && pointer.y > HEADER_HEIGHT) {
            state.touchScrollTarget = 'preview';
        } else {
            state.touchScrollTarget = null;
        }
        state.lastTouchY = pointer.y;
        return;
    }

    if (state.pointerResizeId !== null) {
        setSidebarWidth(width, pointer.x);
        return;
    }

    if (state.touchScrollTarget === null) return;

    const dy = pointer.y - state.lastTouchY;
    state.lastTouchY = pointer.y;

    if (state.touchScrollTarget === 'sidebar') {
        state.scrollY -= dy * TOUCH_SCROLL_SCALE;
    } else if (state.touchScrollTarget === 'preview') {
        state.previewScroll -= dy * TOUCH_SCROLL_SCALE;
    }
}

function toggleAudioPlayback() {
    if (state.audioBuffer === null) return;
    if (state.audioPlayback !== null) {
        try { sys.audio.stopBuffer(state.audioPlayback); } catch (e) { }
        state.audioPlayback = null;
        return;
    }
    state.audioPlayback = sys.audio.playBuffer(state.audioBuffer, state.audioLoop, DEFAULT_AUDIO_GAIN);
}

function ensureVisible(listHeight) {
    const rowTop = state.selectedIndex * ROW_HEIGHT;
    const rowBottom = rowTop + ROW_HEIGHT;
    const visibleHeight = listHeight;

    if (rowTop < state.scrollY) {
        state.scrollY = rowTop;
    } else if (rowBottom > state.scrollY + visibleHeight) {
        state.scrollY = rowBottom - visibleHeight;
    }
}

navigateTo('');

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    updateLayout();
    const input = sys.input.get();

    handleInput(input, width, height);
    updateRowVisuals(input.deltaTime);
    render(width, height);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
