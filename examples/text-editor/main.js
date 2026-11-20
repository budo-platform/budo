import { createUI } from './editor-ui/budo-ui.js';
import { formatByteAddress, formatHex, hexCells, hexRowAddresses, parseHex } from './document.js';
import { FilePicker } from './file-picker.js';
import { draftLimit } from './page.js';
import { Workspace } from './workspace.js';

const ui = createUI();
const dp = ui.dp;
let editorFont;
try {
    sys.font.load('JetBrainsMono-Regular.ttf', 'editor-mono');
    editorFont = 'editor-mono';
} catch (error) {
    console.log('Could not load JetBrains Mono; using the default editor font:', String(error));
}
const offsetField = { value: '' };
const editorZoom = { factor: 1 };
let wordWrap = true;
const hexRowWidths = ['Auto', '16', '32'];
let hexRowWidth = 'Auto';
/** @param {number} columns */
const hexView = columns => ({ scan: hexCells, parse: parseHex, format: bytes => formatHex(bytes, columns) });
const weight = { weight: 1 };
const workspace = new Workspace();
/** @type {import('./editor-ui/lib/widgets/textarea.js').ViewAnchor} */
let viewAnchor = { index: 0, inset: 0 };
/** @typedef {{byte: number, nibble: number}} DocumentCaret */
/** @type {DocumentCaret | null} */
let documentCaret = null;
/** @type {{page: import('./page.js').Page, index: number, value: string} | null} */
let caretProjection = null;
let visibleByte = 0;
/** @type {number | null} */
let restoringByte = null;
let rememberedByte = 0;
/** @type {DocumentCaret | null} */
let rememberedCaret = null;
let nextPositionSave = 0;
/** @type {{ title: string, detail: string, action: () => void } | null} */
let confirmation = null;
/** @type {FilePicker | null} */
let browser = null;
/** @type {FilePicker | null} */
let savePicker = null;
let browserVisible = true;
let dialogTitle = '';
let notice = 'Use the file browser to open or create a document.';
/** @type {number | null} */
let sessionDb = null;

/** @param {() => number} action */
function navigate(action) {
    viewAnchor = { index: action(), inset: 0 };
    documentCaret = caretProjection = null;
    offsetField.value = String(workspace.page.byteAt(viewAnchor.index));
    notice = '';
}

/** @param {import('./page.js').Mode} mode */
function changeMode(mode) {
    const target = documentCaret?.byte ?? visibleByte;
    moveViewport(() => workspace.seekViewport(target, mode), 0, true, !!documentCaret);
    viewAnchor.center = viewAnchor.caret != null;
    rememberSetting('mode', mode);
}

/** @param {import('./page.js').Page} page @param {number} index */
function reportCaret(page, index) {
    const value = page.model.value;
    if (caretProjection?.page === page && caretProjection.index === index && caretProjection.value === value) return;
    documentCaret = page.caretAt(index);
    caretProjection = { page, index, value };
}

/** @param {() => number} action @param {number} inset @param {boolean} updateOffset @param {boolean} [focusCaret] */
function moveViewport(action, inset, updateOffset, focusCaret = false) {
    const index = action();
    const page = workspace.page;
    viewAnchor = { index, inset, continueScroll: !focusCaret, focusCaret };
    caretProjection = null;
    if (documentCaret) {
        const { byte, nibble } = documentCaret;
        const inBuffer = !page.problem && byte >= page.start &&
            (byte < page.end || (byte === page.end && page.end === workspace.size));
        if (inBuffer) {
            const caret = Math.min(page.model.value.length, page.indexAt(byte) + (page.mode === 'Hex' ? nibble : 0));
            viewAnchor.caret = caret;
            // Text can display a whole character for a byte inside it without losing the exact Hex position.
            caretProjection = { page, index: caret, value: page.model.value };
        } else viewAnchor.caret = null;
    }
    if (updateOffset) offsetField.value = String(page.byteAt(index));
    notice = '';
}

/** @param {'mode' | 'hex_row_width' | 'browser_directory' | 'browser_visible'} setting @param {string} value */
function rememberSetting(setting, value) {
    if (sessionDb === null) return;
    try {
        sys.db.run(sessionDb, 'INSERT OR REPLACE INTO display_settings (setting, value) VALUES (?, ?)', setting, value);
    } catch (error) {
        const detail = 'Could not remember the session setting: ' + error.message;
        notice = notice ? notice + '; ' + detail : detail;
    }
}

/** @param {string} rowWidth */
function changeHexRowWidth(rowWidth) {
    if (rowWidth === hexRowWidth) return;
    hexRowWidth = rowWidth;
    rememberSetting('hex_row_width', rowWidth);
}

/** @param {string} path @param {number} offset */
function rememberDocument(path, offset) {
    if (sessionDb === null) return;
    rememberedByte = offset;
    rememberedCaret = path ? documentCaret : null;
    try {
        sys.db.run(sessionDb, `INSERT OR REPLACE INTO last_document
            (id, path, byte_offset, caret_byte, caret_nibble) VALUES (1, ?, ?, ?, ?)`,
        path, offset, rememberedCaret?.byte ?? null, rememberedCaret?.nibble ?? 0);
    } catch (error) {
        notice += '; could not remember this session: ' + error.message;
    }
}

/** @param {number} timestamp */
function rememberPosition(timestamp) {
    if (sessionDb === null || !workspace.path || restoringByte !== null) return;
    if (visibleByte === rememberedByte && documentCaret?.byte === rememberedCaret?.byte &&
        documentCaret?.nibble === rememberedCaret?.nibble) return;
    if (timestamp < nextPositionSave) {
        ui.wakeAfter((nextPositionSave - timestamp) / 1000);
        return;
    }
    rememberDocument(workspace.path, visibleByte);
    nextPositionSave = timestamp + 150;
}

/** @param {string} path @param {number | null} [offset] @param {DocumentCaret | null} [caret] */
function openDocument(path, offset = null, caret = null) {
    workspace.open(path);
    viewAnchor = { index: 0, inset: 0 };
    caretProjection = null;
    documentCaret = caret && (caret.byte < workspace.size ? caret : { byte: workspace.size, nibble: 0 });
    visibleByte = 0;
    restoringByte = null;
    offsetField.value = '0';
    if (offset !== null && (offset > 0 || documentCaret)) {
        const target = Math.min(offset, workspace.size);
        restoringByte = target;
        moveViewport(() => workspace.seekViewport(target), 0, true, !!documentCaret);
        visibleByte = Number(offsetField.value);
    }
    notice = path ? 'Opened ' + path : 'New document';
    if (offset === null) rememberDocument(path, 0);
    else {
        rememberedByte = offset;
        rememberedCaret = caret;
    }
}

function confirm(title, detail, action) {
    confirmation = { title, detail, action };
}

function requestOpen(path, hideBrowser = false) {
    const open = () => {
        openDocument(path);
        if (hideBrowser) setBrowserVisible(false);
    };
    if (workspace.dirty) confirm('Discard changes?', 'Unsaved edits will be lost.', open);
    else open();
}

function saved(path, previousPath) {
    notice = 'Saved ' + path;
    rememberDocument(path, visibleByte);
    if (path !== previousPath) {
        try {
            browser = new FilePicker(false, path);
            rememberSetting('browser_directory', browser.directory);
        } catch (error) {
            notice += '; could not refresh the file browser: ' + error.message;
        }
    }
}

function startSave(path) {
    const previousPath = workspace.path;
    const published = workspace.startSave(path);
    savePicker = null;
    if (published !== null) saved(published, previousPath);
}

function requestSave(path, saveAs = false, hideBrowser = false) {
    const save = () => {
        startSave(path);
        if (hideBrowser) setBrowserVisible(false);
    };
    if (workspace.needsOverwriteConfirmation(path, saveAs))
        confirm('Replace file?', 'Replace the existing destination with this document?', save);
    else save();
}

function saveDocument() {
    if (workspace.path.startsWith('files/')) requestSave(workspace.path);
    else showBrowser(true);
}

/** @param {FilePicker} picker @param {string} directory */
function browseDirectory(picker, directory) {
    picker.browse(directory);
    if (picker === browser) rememberSetting('browser_directory', picker.directory);
}

/** @param {boolean} visible */
function setBrowserVisible(visible) {
    if (browserVisible === visible) return;
    browserVisible = visible;
    rememberSetting('browser_visible', visible ? '1' : '0');
}

function showBrowser(saving = false) {
    if (saving) savePicker = new FilePicker(true, workspace.path);
    else {
        if (!browser) browser = new FilePicker(false, workspace.path);
        savePicker = null;
    }
    setBrowserVisible(true);
}

function toggleBrowser() {
    if (browserVisible) setBrowserVisible(false);
    else if (browser || savePicker) setBrowserVisible(true);
    else showBrowser();
}

function advanceSave() {
    const previousPath = workspace.path;
    const published = workspace.advanceSave();
    if (published !== null) saved(published, previousPath);
}

function cancelSave() {
    workspace.cancelSave();
    notice = 'Save cancelled; destination unchanged.';
}

/** @param {() => void} action @param {FilePicker | null} [picker] */
function attempt(action, picker = null) {
    try {
        action();
    } catch (error) {
        if (picker) picker.error = error.message;
        else notice = error.message;
    }
}

function restoreSession() {
    let document, settings;
    let browserDirectory;
    try {
        const handle = sys.db.open('text-editor-session');
        try {
            sys.db.execute(handle, `CREATE TABLE IF NOT EXISTS last_document (id INTEGER PRIMARY KEY, path TEXT NOT NULL,
                byte_offset INTEGER NOT NULL DEFAULT 0, caret_byte INTEGER, caret_nibble INTEGER NOT NULL DEFAULT 0)`);
            const columns = new Set(sys.db.query(handle, 'PRAGMA table_info(last_document)').map(column => column.name));
            for (const [name, type] of [['byte_offset', 'INTEGER NOT NULL DEFAULT 0'],
                ['caret_byte', 'INTEGER'], ['caret_nibble', 'INTEGER NOT NULL DEFAULT 0']]) {
                if (!columns.has(name)) sys.db.execute(handle, `ALTER TABLE last_document ADD COLUMN ${name} ${type}`);
            }
            document = sys.db.query(handle, 'SELECT path, byte_offset, caret_byte, caret_nibble FROM last_document WHERE id = 1')[0];
            sys.db.execute(handle, 'CREATE TABLE IF NOT EXISTS display_settings (setting TEXT PRIMARY KEY, value TEXT NOT NULL)');
            settings = sys.db.query(handle, 'SELECT setting, value FROM display_settings');
        } catch (error) {
            sys.db.close(handle);
            throw error;
        }
        sessionDb = handle;
    } catch (error) {
        notice = 'Could not load the last editor session: ' + error.message;
    }
    for (const { setting, value } of settings ?? []) {
        if (setting === 'mode' && value === 'Hex') workspace.seekViewport(0, 'Hex');
        if (setting === 'hex_row_width' && hexRowWidths.includes(value)) hexRowWidth = value;
        if (setting === 'browser_directory') browserDirectory = value;
        if (setting === 'browser_visible' && value === '0') browserVisible = false;
    }
    if (document?.path) {
        try {
            const offset = document.byte_offset;
            const byte = document.caret_byte;
            const caret = Number.isSafeInteger(byte) && byte >= 0 ? { byte, nibble: document.caret_nibble === 1 ? 1 : 0 } : null;
            openDocument(document.path, Number.isSafeInteger(offset) && offset >= 0 ? offset : 0, caret);
        } catch (error) {
            notice = 'Could not reopen ' + document.path + ': ' + error.message;
        }
    }
    if (browserDirectory !== undefined) {
        try {
            browser = new FilePicker(false, workspace.path, browserDirectory);
        } catch (error) {
            notice += '; could not restore the browser folder: ' + error.message;
        }
    }
    if (!browser) attempt(() => {
        browser = new FilePicker(false, workspace.path);
        if (browserDirectory !== undefined) rememberSetting('browser_directory', browser.directory);
    });
}

restoreSession();

function frame(timestamp) {
    const input = sys.input.get();
    ui.begin(input);
    ui.clear();
    let command = null;
    let reportedViewport = false, applyingHexLayout = false;
    const { page, saving } = workspace;
    const { mode } = page;
    const shell = ui.inset(ui.bounds, dp(14));
    const gap = dp(6);
    const buttonSize = ui.sp(ui.theme.font);
    const buttonHeight = Math.max(dp(36), buttonSize + dp(14));
    const sidebarWidth = Math.min(dp(300), Math.max(dp(240), Math.round(shell.width * 0.32)));
    const showSidebar = browserVisible && !saving;
    const narrow = showSidebar && shell.width < sidebarWidth + gap + dp(340);
    const [sidebar, bounds] = showSidebar && !narrow
        ? ui.columns(shell, [sidebarWidth, weight], gap) : [shell, shell];
    const toggleWidth = !showSidebar && !saving ? buttonHeight + gap : 0;
    let commandPicker = null;
    if (!saving) {
        const toggle = { ...sidebar, width: buttonHeight, height: buttonHeight };
        if (ui.iconButton('toggle-browser', showSidebar ? 'chevronLeft' : 'folder', toggle, {
            variant: 'secondary', label: showSidebar ? 'Hide file browser' : 'Show file browser',
        })) command = toggleBrowser;
    }
    if (showSidebar) {
        const picker = savePicker || browser;
        const [heading, content] = ui.rows(sidebar, [buttonHeight, weight], gap);
        const title = { ...heading, x: heading.x + buttonHeight + gap, width: Math.max(0, heading.width - buttonHeight - gap) };
        ui.paragraph(savePicker ? 'Save As' : 'Files', { ...title, y: title.y + dp(7) }, { maxLines: 1 });
        if (!narrow) ui.fill({ x: sidebar.x + sidebar.width + gap / 2, y: sidebar.y, width: dp(1), height: sidebar.height },
            ui.colors.border, 0);
        if (!picker) ui.paragraph(notice, content, { maxLines: 3 });
        else if (content.height >= dp(190)) {
            ui.scope(savePicker ? 'save-browser' : 'file-browser', () => {
                const action = picker.draw(ui, content, {
                    browse: directory => browseDirectory(picker, directory),
                    open: path => requestOpen(path, narrow),
                    save: path => requestSave(path, true, narrow),
                    close: () => { savePicker = null; },
                });
                if (action) {
                    command = action;
                    commandPicker = picker;
                }
            });
        } else ui.paragraph('Enlarge the window to browse files.', content, { maxLines: 2 });
    }
    const buttonWidth = label => Math.ceil(ui.buttonWidth(label) / 0.965);
    const saveWidth = buttonWidth('Save');
    const saveAsWidth = buttonWidth('Save As');
    const cancelWidth = buttonWidth('Cancel');
    const minNameWidth = dp(56);
    const headerWidth = bounds.width - toggleWidth;
    const splitSaveButtons = !saving && saveWidth + saveAsWidth + gap > headerWidth;
    const toolbarRows = splitSaveButtons ? 2 : 1;
    const toolbarWidth = saving ? cancelWidth : Math.max(saveWidth, saveAsWidth);
    const toolbarHeight = toolbarRows * buttonHeight + (toolbarRows - 1) * gap;
    const firstButtonsWidth = saving ? cancelWidth : splitSaveButtons ? saveWidth : saveWidth + saveAsWidth + gap;
    const separateName = headerWidth - firstButtonsWidth - gap < minNameWidth;
    const nameSize = ui.sp(18);
    const nameHeight = Math.ceil(nameSize * 1.25);
    const headerHeight = toolbarHeight + (separateName ? nameHeight + gap : 0);
    const splitHexNavigation = !saving && mode === 'Hex' && bounds.width < dp(345);
    const navigationHeight = dp(36) + (splitHexNavigation ? dp(36) + gap : 0);
    if (!narrow && (bounds.width < dp(255) || headerWidth < toolbarWidth ||
        bounds.height < dp(241) + headerHeight + navigationHeight - dp(72))) {
        ui.label('Enlarge the window to edit', bounds);
    } else if (!narrow) {
        const [headerArea, navigation, editor, status] = ui.rows(bounds,
            [headerHeight, navigationHeight, weight, dp(25)], dp(7));
        const header = { ...headerArea, x: headerArea.x + toggleWidth, width: headerWidth };
        const [nameRow, toolbar] = separateName ? ui.rows(header, [nameHeight, toolbarHeight], gap) : [null, header];
        function toolbarRow(row, widths) {
            const width = widths.reduce((sum, w) => sum + w, 0) + (widths.length - 1) * gap;
            const space = row.width - width;
            return [
                { ...row, width: Math.max(0, space - gap) },
                ...ui.columns({ ...row, x: row.x + space, width }, widths, gap),
            ];
        }
        let title, save, saveAs;
        if (saving) {
            let cancel;
            [title, cancel] = toolbarRow(toolbar, [cancelWidth]);
            if (ui.button('cancel-save', 'Cancel', cancel)) command = cancelSave;
        } else if (splitSaveButtons) {
            const [saveRow, saveAsRow] = ui.rows(toolbar, [buttonHeight, buttonHeight], gap);
            [title, save] = toolbarRow(saveRow, [saveWidth]);
            [, saveAs] = toolbarRow(saveAsRow, [saveAsWidth]);
        } else {
            [title, save, saveAs] = toolbarRow(toolbar, [saveWidth, saveAsWidth]);
        }
        if (!saving) {
            if (ui.button('save', 'Save', save, { primary: true })) command = saveDocument;
            if (ui.button('save-as', 'Save As', saveAs)) command = () => showBrowser(true);
        }
        const filePath = saving ? workspace.savePath : workspace.path;
        const filename = filePath ? filePath.slice(filePath.lastIndexOf('/') + 1) : 'Untitled';
        const name = (saving ? 'Saving ' : workspace.dirty ? '* ' : '') + filename;
        const nameRect = nameRow || title;
        ui.paragraph(name, { ...nameRect, y: nameRect.y + Math.max(0, (nameRect.height - nameHeight) / 2) },
            { size: nameSize, maxLines: 1 });

        let rowWidth = hexRowWidth;
        if (!saving) {
            const modeWidth = dp(navigation.width < dp(440) ? 76 : 130);
            const [modeArea, hexControls] = splitHexNavigation
                ? ui.rows(navigation, [dp(36), dp(36)], gap)
                : ui.columns(navigation, [modeWidth, weight], dp(5));
            const modes = { ...modeArea, width: modeWidth };
            const picked = ui.segmented('mode', modes, ['Text', 'Hex'], mode, { label: 'Editing mode' });
            if (picked !== mode) command = () => changeMode(picked);
            if (mode === 'Hex') {
                const [offset, go, rowToggle] = ui.columns(hexControls, [weight, dp(42), dp(118)], dp(5));
                ui.field('offset', offset, offsetField, { label: 'Byte offset', placeholder: 'Byte offset', maxLength: 20 });
                if (ui.button('goto', 'Go', go)) command = () => navigate(() => workspace.go(offsetField.value.trim()));
                rowWidth = ui.segmented('hex-row-width', rowToggle, hexRowWidths, hexRowWidth, { label: 'Hex bytes per row' });
            }
        } else ui.progress('save-progress', navigation, workspace.saveProgress, { label: 'Saving file' });

        const hexDraftLimit = Math.max(draftLimit, workspace.hexCapacity * 3 + 1024);
        const maxAddress = Math.max(workspace.size, page.start + hexDraftLimit / 2);
        const addressWidth = Math.max(8, maxAddress.toString(16).length);
        const [statusText, wrapToggle] = ui.columns(status, [weight, ui.textWidth('Wrap') + dp(40)], gap);
        wordWrap = ui.checkbox('word-wrap', 'Wrap', wrapToggle, wordWrap);
        const rowWidthChanged = rowWidth !== hexRowWidth;
        let fitColumns = workspace.hexColumns, visibleRows = 0;
        const gutter = mode === 'Hex' ? {
            sample: formatByteAddress(maxAddress, addressWidth),
            labels: (text, starts) => hexRowAddresses(text, starts, page.start, addressWidth),
        } : undefined;
        ui.textArea('page', editor, page.model, {
            resetKey: page.revision, label: mode === 'Text' ? 'UTF-8 text page' : 'Hexadecimal byte page',
            placeholder: page.problem || (mode === 'Text' ? 'Type here' : 'Type hex pairs, e.g. 41 42 FF'),
            maxLength: mode === 'Hex' ? hexDraftLimit : draftLimit, readOnly: saving || !!page.problem,
            size: ui.sp(14), font: editorFont, gutter, zoom: editorZoom, wrap: wordWrap,
            byteView: mode === 'Hex' ? hexView(workspace.hexColumns) : undefined, byteAppend: page.end === workspace.size,
            hexGeometry: (columns, rows) => { fitColumns = columns; visibleRows = rows; },
            viewport: {
                size: workspace.size, anchor: viewAnchor,
                before: page.start > 0, after: page.end < workspace.size,
                byteAt: index => page.byteAt(index),
                report: index => {
                    visibleByte = page.byteAt(index);
                    reportedViewport = true;
                },
                reportCaret: index => reportCaret(page, index),
                seek: offset => {
                    if (!saving && !command) command = () => moveViewport(() => workspace.seekViewport(offset), 0, true);
                },
                edge: (index, delta, inset, pageMove) => {
                    if (saving || command) return false;
                    command = () => {
                        const direction = Math.sign(delta);
                        const offset = page.problem && direction > 0 ? page.end : page.byteAt(index);
                        moveViewport(() => workspace.seekViewport(offset, mode, pageMove?.rows === 0 ? 0 : direction), inset, false);
                        if (pageMove) {
                            viewAnchor.caret = viewAnchor.index;
                            viewAnchor.pageMove = pageMove;
                        }
                    };
                    return true;
                },
            },
        });
        if (mode === 'Hex' && !saving && (rowWidthChanged || (!command && !page.pending))) {
            const columns = rowWidth === 'Auto' ? 2 ** Math.floor(Math.log2(Math.min(256, fitColumns))) : Number(rowWidth);
            // Keep about two screens of rows, including overlap for continuous scrolling.
            const capacity = columns * Math.min(Math.floor(8192 / columns),
                Math.max(Math.ceil(512 / columns), 2 * visibleRows + 4));
            if (columns !== workspace.hexColumns || capacity > workspace.hexCapacity) {
                applyingHexLayout = true;
                command = () => {
                    const center = viewAnchor.center;
                    const target = restoringByte ?? (center && documentCaret ? documentCaret.byte : visibleByte);
                    moveViewport(() => workspace.setHexLayout(columns, Math.max(capacity,
                        columns * Math.floor(workspace.hexCapacity / columns)), target), 0, true);
                    viewAnchor.center = center;
                    changeHexRowWidth(rowWidth);
                };
            } else if (rowWidthChanged) command = () => changeHexRowWidth(rowWidth);
        }

        const help = mode === 'Text' ? 'UTF-8'
            : '0x' + visibleByte.toString(16).toUpperCase() + '  |  ' + workspace.size.toLocaleString() + ' bytes';
        ui.paragraph(notice || help, statusText, { maxLines: 1 });
    }

    if (!confirmation && !saving && (input.keyboard.ctrl || input.keyboard.meta)) {
        if (sys.input.isKeyPressed(18)) {
            command = () => showBrowser(); // O
            commandPicker = null;
        }
        if (sys.input.isKeyPressed(22)) {
            command = input.keyboard.shift ? () => showBrowser(true) : saveDocument; // S
            commandPicker = null;
        }
    }
    const activeConfirmation = confirmation;
    if (activeConfirmation) {
        dialogTitle = activeConfirmation.title;
        command = null;
        commandPicker = savePicker || (browserVisible ? browser : null);
    }
    if (ui.dialog('file-dialog', !!activeConfirmation, content => {
        if (!activeConfirmation) return;
        const [message, , buttons] = ui.rows(content, [dp(38), weight, dp(36)], dp(4));
        ui.label(activeConfirmation.detail, message);
        const [cancel, accept] = ui.columns(buttons, [weight, weight], dp(8));
        if (ui.button('no', 'Cancel', cancel)) confirmation = null;
        if (ui.button('yes', 'Continue', accept, { danger: true })) {
            confirmation = null;
            command = activeConfirmation.action;
        }
    }, {
        title: dialogTitle, width: dp(440), height: dp(154),
    })) {
        command = () => { confirmation = null; };
    }

    ui.end();
    // The initial Hex buffer may be shorter than the viewport until Auto sizing runs.
    if (reportedViewport && !applyingHexLayout) {
        restoringByte = null;
        viewAnchor.center = false;
    }
    rememberPosition(timestamp);
    if (command) attempt(command, commandPicker);
    if (workspace.saving && saving) {
        attempt(advanceSave);
        // Paint completion or failure even if focus is idle and the job ended.
        sys.animation.requestFrame(frame);
    } else if (workspace.saving || command) sys.animation.requestFrame(frame);
    else ui.nextFrame(frame);
}

sys.animation.requestFrame(frame);
