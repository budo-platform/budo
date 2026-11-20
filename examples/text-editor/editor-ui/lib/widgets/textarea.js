/** Multiline text: wrapped lines, drag and double-click selection, word and
 *  line navigation, held-key repeat, clipboard, undo and redo, scrolling that
 *  follows the caret. Text arrives through Budo text sessions (IME, soft
 *  keyboards); where the platform edits natively (web, Android), it owns the
 *  text changes and undo, and the widget keeps navigation and selection. */

import { clamp } from '../math.js';
import { contains } from '../layout.js';
import { springs } from '../motion.js';
import { withAlpha } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {{ value: string }} TextModel */
/** @typedef {{ sample: string, labels: (text: string, starts: number[]) => string[] }} TextGutter */
/** @typedef {{ start: number, end: number, char: string }} ByteCell */
/** @typedef {{ scan: (text: string) => ByteCell[], parse: (text: string) => Uint8Array,
 *   format: (bytes: Uint8Array) => string }} ByteView */
/** @typedef {{ rows: number, column: number }} PageMove */
/** @typedef {{ index: number, inset: number, continueScroll?: boolean, caret?: number | null, focusCaret?: boolean,
 *   pageMove?: PageMove, center?: boolean }} ViewAnchor */
/** @typedef {{ before: boolean, after: boolean, size: number, anchor: ViewAnchor,
 *   byteAt: (index: number) => number, report: (index: number) => void, reportCaret?: (index: number) => void,
 *   seek: (offset: number) => void, edge: (index: number, delta: number, inset: number, pageMove?: PageMove) => boolean }} DocumentViewport */
/** @typedef {{ placeholder?: string, label?: string, maxLength?: number, readOnly?: boolean,
 *   size?: number, font?: string, resetKey?: number, gutter?: TextGutter,
 *   zoom?: { factor: number }, wrap?: boolean, byteView?: ByteView, byteAppend?: boolean,
 *   viewport?: DocumentViewport, hexGeometry?: (columns: number, rows: number) => void }} TextAreaOptions  `font`: a name loaded with sys.font.load */
/** @typedef {{ start: number, end: number }} Line  UTF-16 range of a visual line, without its newline */
/** @typedef {Line & { logicalStart: number }} VisualLine */
/** @typedef {{ value: string, start: number, end: number }} Snapshot */

// USB HID scancodes
const KEY_A = 4, KEY_C = 6, KEY_V = 25, KEY_X = 27, KEY_Y = 28, KEY_Z = 29;
const KEY_ENTER = 40, KEY_BACKSPACE = 42, KEY_PAGE_UP = 75, KEY_HOME = 74, KEY_DELETE = 76, KEY_END = 77,
    KEY_PAGE_DOWN = 78, KEY_RIGHT = 79, KEY_LEFT = 80, KEY_DOWN = 81, KEY_UP = 82;
const HISTORY_LIMIT = 200;

/** Word characters for word navigation and double-click selection. */
const WORD = /[\p{L}\p{N}_]/u;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;
    /** @type {Map<string, any>} per-widget editing state */
    const editors = new Map();
    /** @type {Map<string, Line[]>} wrapped lines of one paragraph, by font, size, width, and text */
    const wrapped = new Map();
    /** @type {string | null} the font of the text area being drawn (null: the default font) */
    let font = null;
    const measure = (value, size) => ctx.measure(value, size, font);

    // ── Layout ─────────────────────────────────────────────────────────────

    /** Visual lines of a paragraph (no newline inside), relative to its start. */
    function wrapParagraph(text, width, size) {
        const key = (font || '') + '|' + size + '|' + Math.round(width) + '|' + text;
        let lines = wrapped.get(key);
        if (lines) return lines;
        lines = [];
        const measure = (from, to) => sys.canvas.measureText(text.slice(from, to), size);
        let start = 0;
        while (start < text.length) {
            if (measure(start, text.length) <= width) {
                lines.push({ start, end: text.length });
                break;
            }
            // Greedy: take words (with their trailing spaces) while they fit.
            let end = start, fits = start;
            const words = /\S*\s*/gy;
            words.lastIndex = start;
            while (words.lastIndex < text.length) {
                const match = words.exec(text);
                if (!match || !match[0]) break;
                end = words.lastIndex;
                const visible = text.slice(start, end).trimEnd().length + start;
                if (measure(start, visible) > width) break;
                fits = end;
            }
            if (fits === start) {
                // One word wider than the line: break it between characters.
                fits = start + 1;
                while (fits < text.length && measure(start, fits + 1) <= width) fits++;
                if (/[\uDC00-\uDFFF]/.test(text[fits] || '')) fits++;
            }
            lines.push({ start, end: fits });
            start = fits;
        }
        if (!lines.length) lines.push({ start: 0, end: 0 });
        if (wrapped.size > 2000) wrapped.clear();
        wrapped.set(key, lines);
        return lines;
    }

    /** Visual lines of the whole text. @returns {VisualLine[]} */
    function layout(text, width, size, wrap) {
        const lines = [];
        let offset = 0;
        for (const paragraph of text.split('\n')) {
            if (wrap) {
                for (const line of wrapParagraph(paragraph, width, size))
                    lines.push({ start: offset + line.start, end: offset + line.end, logicalStart: offset });
            } else lines.push({ start: offset, end: offset + paragraph.length, logicalStart: offset });
            offset += paragraph.length + 1;
        }
        return lines;
    }

    /** The line holding `index`; at a soft wrap the caret belongs to the next line. */
    function lineOf(lines, index) {
        for (let i = 0; i < lines.length; i++)
            if (index < lines[i].end || (index === lines[i].end && (i + 1 === lines.length || lines[i + 1].start > index)))
                return i;
        return lines.length - 1;
    }

    /** Nearest index on `line` to horizontal offset `x`. */
    function indexInLine(text, line, x, size) {
        if (x <= 0) return line.start;
        const widthAt = index => measure(text.slice(line.start, index), size);
        let low = line.start, high = line.end;
        while (low < high) {
            const mid = Math.floor((low + high) / 2);
            if (widthAt(mid) < x) low = mid + 1;
            else high = mid;
        }
        const right = isLow(text[low]) ? Math.min(line.end, low + 1) : low;
        const left = Math.max(line.start, stepLeft(text, right));
        return x < (widthAt(left) + widthAt(right)) / 2 ? left : right;
    }

    // ── Editing helpers ────────────────────────────────────────────────────

    const isLow = c => /[\uDC00-\uDFFF]/.test(c || '');
    const isHigh = c => /[\uD800-\uDBFF]/.test(c || '');
    const stepLeft = (text, at) => at > 0 ? at - (isLow(text[at - 1]) ? 2 : 1) : 0;
    const stepRight = (text, at) => at < text.length ? at + (isHigh(text[at]) ? 2 : 1) : text.length;
    const isSeparator = char => /\s/.test(char || '');
    function skipSeparators(text, at, direction = 1) {
        while (at >= 0 && isSeparator(text[at])) at += direction;
        // Moving left past leading whitespace lands on the first editable digit.
        if (at < 0) return skipSeparators(text, 0);
        return at;
    }

    function wordLeft(text, at) {
        while (at > 0 && !WORD.test(text[at - 1])) at--;
        while (at > 0 && WORD.test(text[at - 1])) at--;
        return at;
    }
    function wordRight(text, at) {
        while (at < text.length && !WORD.test(text[at])) at++;
        while (at < text.length && WORD.test(text[at])) at++;
        return at;
    }
    /** The word (or run of other characters) around `at`, preferring the
     *  word that ends at `at`. Never crosses a line break. */
    function wordAt(text, at) {
        const inWord = i => WORD.test(text[i] || '');
        let probe = at;
        if (!inWord(probe) && probe > 0 && inWord(probe - 1)) probe--;
        if (probe >= text.length || text[probe] === '\n') {
            if (probe === 0 || text[probe - 1] === '\n') return { start: at, end: at };
            probe--;
        }
        const kind = inWord(probe);
        let start = probe, end = probe + 1;
        while (start > 0 && text[start - 1] !== '\n' && inWord(start - 1) === kind) start--;
        while (end < text.length && text[end] !== '\n' && inWord(end) === kind) end++;
        return { start, end };
    }

    const repeated = scancode => ctx.keyRepeated(scancode);
    const pressedOnce = scancode => sys.input.isKeyPressed(scancode);
    const command = () => {
        const keyboard = ctx.input.keyboard;
        return !!keyboard && (keyboard.ctrl || keyboard.meta);
    };

    function editorFor(key, model) {
        let editor = editors.get(key);
        if (!editor) {
            editor = {
                start: model.value.length, end: model.value.length, anchor: model.value.length,
                composition: '', compositionCaret: 0,
                goalX: null, undo: [], redo: [], lastEdit: null, lastPress: { time: -1, x: 0, y: 0, count: 0 },
                lastCaret: -1, dragging: false, pinch: null, scrollX: 0, asciiScrollX: 0, ascii: false, parked: false,
            };
            editors.set(key, editor);
        }
        return editor;
    }

    /** Record the state before an edit; typing in a row (and deleting in a
     *  row) makes one undo step. @param {string} kind */
    function remember(editor, model, kind) {
        const now = ctx.input.totalTime || 0;
        const last = editor.lastEdit;
        const coalesce = last && last.kind === kind && kind !== 'other' && now - last.time < 1 && last.caret === editor.start;
        if (!coalesce) {
            editor.undo.push({ value: model.value, start: editor.start, end: editor.end });
            if (editor.undo.length > HISTORY_LIMIT) editor.undo.shift();
        }
        editor.redo.length = 0;
        editor.lastEdit = { kind, time: now, caret: -1 };
    }

    function replaceSelection(editor, model, text, maxLength) {
        if (maxLength) text = text.slice(0, Math.max(0, maxLength - (model.value.length - (editor.end - editor.start))));
        model.value = model.value.slice(0, editor.start) + text + model.value.slice(editor.end);
        editor.start = editor.end = editor.anchor = editor.start + text.length;
        if (editor.lastEdit) editor.lastEdit.caret = editor.start;
    }

    function startSession(key, editor, model) {
        if (ctx.focus === key) return;
        if (ctx.focus !== null) sys.input.stopTextInput();
        ctx.focus = key;
        editor.composition = '';
        editor.compositionCaret = 0;
        sys.input.startTextInput({
            text: editor.ascii ? '' : model.value, selectionStart: editor.ascii ? 0 : editor.start,
            selectionEnd: editor.ascii ? 0 : editor.end, multiline: true,
        });
    }

    /** @param {VisualLine[]} lines @param {ByteCell[]} cells */
    function byteRows(lines, cells) {
        /** @type {ByteCell[][]} */
        const rows = lines.map(() => []);
        let row = 0;
        for (const cell of cells) {
            while (row + 1 < lines.length && cell.start >= lines[row].end) row++;
            rows[row].push(cell);
        }
        return rows;
    }

    /** @param {ByteCell[]} cells @param {number} index */
    function byteAt(cells, index) {
        const at = cells.findIndex(cell => index < cell.end);
        return at < 0 ? cells.length : at;
    }

    /** @param {ByteView} view @param {TextModel} model @param {string} input @param {TextAreaOptions} options */
    function editAscii(view, editor, model, input, options, direction = 0) {
        let bytes;
        try {
            bytes = view.parse(model.value);
        } catch (error) {
            return ui.toast(error.message, { kind: 'danger' });
        }
        if (/[^\x00-\x7f]/.test(input))
            return ui.toast('Enter ASCII characters (0–127), or edit the hex bytes.', { kind: 'danger' });
        const cells = view.scan(model.value);
        let from = byteAt(cells, editor.start), to = byteAt(cells, editor.end);
        if (editor.end > editor.start && to < cells.length && editor.end > cells[to].start) to++;
        if (editor.start === editor.end) {
            if (direction < 0) from = Math.max(0, from - 1);
            else to = Math.min(bytes.length, from + (direction ? 1 : input.length));
            if (from + input.length > bytes.length && !options.byteAppend)
                return ui.toast('End of this page. Use Next to edit the following bytes.', { kind: 'danger' });
        }
        if (from === to && !input.length) return;
        const next = new Uint8Array(bytes.length - (to - from) + input.length);
        next.set(bytes.subarray(0, from));
        for (let i = 0; i < input.length; i++) next[from + i] = input.charCodeAt(i);
        next.set(bytes.subarray(to), from + input.length);
        let text;
        if (input.length === to - from) {
            // Replace only the two nibbles, keeping any manually entered whitespace.
            const parts = [];
            let at = 0;
            for (let i = from; i < to; i++) {
                const { start, end } = cells[i];
                const pair = next[i].toString(16).padStart(2, '0').toUpperCase();
                parts.push(model.value.slice(at, start), pair[0], model.value.slice(start + 1, end - 1), pair[1]);
                at = end;
            }
            text = parts.join('') + model.value.slice(at);
        } else text = view.format(next);
        if (options.maxLength && text.length > options.maxLength)
            return ui.toast('This page is full.', { kind: 'danger' });
        if (model.value !== text) remember(editor, model, 'other');
        model.value = text;
        const at = view.scan(text)[from + input.length]?.start ?? text.length;
        editor.start = editor.end = editor.anchor = at;
        editor.goalX = null;
    }

    // ── The widget ─────────────────────────────────────────────────────────

    /** @param {Rect} rect @param {string} key @param {{factor: number}} [zoom] */
    function updateZoom(rect, key, editor, zoom) {
        const input = ctx.input;
        if (!zoom || !input.focused || (ctx.modalOpen && ctx.z === 0)) {
            editor.pinch = null;
            return { pinching: false, zoomWheel: false };
        }
        const view = ctx.toScreen(rect);
        /** @param {{x: number, y: number}} p */
        const inside = p => contains(view, p.x, p.y) && (!ctx.clip || contains(ctx.clip, p.x, p.y));
        const touches = input.pointers.filter(p => p.type === 'touch' && p.down);
        if (!touches.length) editor.pinch = null;
        else if (!editor.pinch && touches.length === 2 && touches.every(inside)) {
            editor.pinch = { ids: touches.map(p => p.id), distance: 0 };
        }
        if (editor.pinch) {
            // Keep the remaining finger from turning the pinch into a tap or drag.
            const [a, b] = editor.pinch.ids.map(id => touches.find(p => p.id === id));
            const distance = a && b ? Math.hypot(a.x - b.x, a.y - b.y) : 0;
            if (distance > 0 && editor.pinch.distance > 0)
                zoom.factor = clamp(zoom.factor * distance / editor.pinch.distance, 0.6, 3);
            editor.pinch.distance = distance;
            editor.dragging = false;
            if (ctx.active === key) ctx.active = null;
            return { pinching: true, zoomWheel: true };
        }
        const trackpad = input.pinch;
        if (trackpad && inside(trackpad))
            zoom.factor = clamp(zoom.factor * trackpad.scale, 0.6, 3);
        const zoomWheel = input.keyboard.ctrl && !!input.mouse.wheelY && inside(input.mouse);
        if (zoomWheel) zoom.factor = clamp(zoom.factor * 1.1 ** input.mouse.wheelY, 0.6, 3);
        return { pinching: false, zoomWheel };
    }

    /** Multiline text editing `model.value` in place; wraps to the width and
     *  scrolls. Enter inserts a line break.
     *  @param {string} id @param {Rect} rect @param {TextModel} model
     *  @param {TextAreaOptions} [options] @returns {string} the current value */
    function textArea(id, rect, model, options = {}) {
        const state = ctx.interact(id, rect, {
            cursor: 'text',
            a11y: { role: 'textbox', label: options.label || options.placeholder || '', value: model.value },
        });
        const key = state.id;
        const input = ctx.input, theme = t(), px = ctx.px;
        font = null;
        if (options.font && typeof sys.canvas.setFont === 'function') {
            try {
                sys.canvas.setFont(options.font);
                font = options.font;
            } catch (e) { /* unknown font: keep the default */ }
        }
        const readOnly = !!options.readOnly;
        const viewport = options.viewport;
        const scrollId = id + ':scroll';
        const previous = editors.get(key);
        let resetScroll = false;
        if (options.resetKey !== undefined && editors.get(key)?.resetKey !== options.resetKey) {
            editors.delete(key);
            if (ctx.focus === key && !viewport?.anchor.continueScroll) {
                sys.input.stopTextInput();
                ctx.focus = null;
            }
            resetScroll = true;
        }
        const editor = editorFor(key, model);
        if (resetScroll && viewport) {
            const { anchor } = viewport;
            editor.start = editor.end = editor.anchor = anchor.caret ?? anchor.index;
            editor.parked = anchor.caret === null;
            if (options.byteView && anchor.continueScroll && previous) {
                editor.ascii = previous.ascii;
                editor.scrollX = previous.scrollX;
                editor.asciiScrollX = previous.asciiScrollX;
            }
            if (anchor.focusCaret && !readOnly) {
                ctx.focusKey(key);
                startSession(key, editor, model);
            }
        }
        editor.resetKey = options.resetKey;
        const { pinching, zoomWheel } = updateZoom(rect, key, editor, options.zoom);
        const baseSize = options.size || ctx.sp(theme.font);
        const size = options.zoom ? Math.round(baseSize * options.zoom.factor) : baseSize;
        const lineHeight = Math.round(size * 1.4);
        const wrap = options.wrap !== false;
        const scrollBarHeight = wrap ? 0 : px(12);
        const pointer = input.pointer;
        const inner = { x: rect.x + px(12), y: rect.y + px(10), width: Math.max(0, rect.width - px(24)),
            height: Math.max(0, rect.height - px(20) - scrollBarHeight) };
        const gutter = options.gutter;
        const bytes = options.byteView;
        const cellWidth = bytes ? measure('0', size) : 0;
        const addressWidth = gutter ? measure(gutter.sample, size) : 0;
        const gutterWidth = gutter ? Math.max(0, Math.min(addressWidth + px(20),
            bytes ? inner.width * 0.4 : inner.width - px(64))) : 0;
        const gutterSize = addressWidth > 0
            ? Math.max(1, Math.floor(size * Math.min(1, Math.max(0, gutterWidth - px(20)) / addressWidth))) : size;
        const columnsWidth = Math.max(0, inner.width - gutterWidth - (bytes || viewport ? px(12) : 0));
        const asciiShare = options.hexGeometry ? (columnsWidth + cellWidth + px(18)) / 4 : (columnsWidth + px(12)) / 3;
        const asciiWidth = bytes ? Math.min(columnsWidth / 2, Math.max(asciiShare, cellWidth + px(12))) : 0;
        const textWidth = columnsWidth - asciiWidth;
        const asciiX = inner.x + gutterWidth + textWidth;
        const asciiColumns = Math.max(1, Math.floor((asciiWidth - px(6)) / Math.max(1, cellWidth)));
        if (bytes && options.hexGeometry) {
            const hexColumns = Math.floor((textWidth / Math.max(1, cellWidth) + 1) / 3);
            options.hexGeometry(Math.max(1, Math.min(hexColumns, asciiColumns)), Math.ceil(inner.height / lineHeight));
        }
        const scrollKey = ctx.fullId(scrollId);

        if (state.action?.action === 'setValue' && !readOnly) {
            remember(editor, model, 'other');
            model.value = options.maxLength ? state.action.value.slice(0, options.maxLength) : state.action.value;
            editor.start = editor.end = editor.anchor = model.value.length;
            editor.parked = false;
        }
        if (state.action) startSession(key, editor, model);
        // Keyboard focus arrived (Tab): edit with the caret where it was.
        else if (state.focused && ctx.focus !== key && ctx.focusVisible) startSession(key, editor, model);
        // In a scrolling page, stay in view while the on-screen keyboard opens.
        if (ctx.focus === key) ctx.revealAboveKeyboard(rect);

        // Wrap the shown text (with an IME composition inside) to the width
        // left after the scrollbar gutter, then decide where the scrollbar goes.
        const composing = ctx.focus === key && !editor.ascii && !!editor.composition;
        const shown = composing
            ? model.value.slice(0, editor.start) + editor.composition + model.value.slice(editor.end) : model.value;
        function layoutContent(value) {
            // A draft may omit all separators, so allow for two glyphs per byte.
            const normalPairs = options.hexGeometry && !/[0-9a-f]{3}/i.test(value);
            const width = bytes && wrap ? Math.min(textWidth, (normalPairs ? 3 * asciiColumns - 1 : 2 * asciiColumns) * cellWidth)
                : textWidth;
            const rows = layout(value, width, size, wrap);
            return !bytes && !viewport && wrap && rows.length * lineHeight > inner.height ? layout(value, textWidth - px(12), size, wrap) : rows;
        }
        let lines = layoutContent(shown);
        let cells = bytes ? bytes.scan(shown) : [];
        let characters = bytes ? byteRows(lines, cells) : [];
        const centeredOffset = row => (row + 0.5) * lineHeight - inner.height / 2;
        let initialOffset = 0;
        if (resetScroll && viewport) {
            const { anchor } = viewport;
            const row = lineOf(lines, anchor.center ? anchor.caret ?? anchor.index : anchor.index);
            const target = anchor.center ? centeredOffset(row) : row * lineHeight + anchor.inset;
            initialOffset = clamp(target, 0, Math.max(0, lines.length * lineHeight - inner.height));
        }
        const offset = resetScroll ? initialOffset : ctx.motion.get('n:' + scrollKey + '#offset')?.spring.value || 0;
        if (wrap) editor.scrollX = editor.asciiScrollX = 0;
        const horizontalWheel = !wrap && !zoomWheel && !pinching && !(ctx.modalOpen && ctx.z === 0) && ctx.inside(inner)
            ? input.mouse.wheelX - (input.keyboard.shift ? input.mouse.wheelY : 0) : 0;
        if (horizontalWheel) {
            const delta = horizontalWheel * px(38);
            editor.scrollX = Math.max(0, editor.scrollX + delta);
            if (bytes) editor.asciiScrollX = Math.max(0, editor.asciiScrollX + delta / 3);
        }
        const indexAt = (x, y) => {
            const row = clamp(Math.floor((y - inner.y + offset) / lineHeight), 0, lines.length - 1);
            const line = lines[row];
            if (bytes && editor.ascii) {
                const index = Math.max(0, Math.floor((x - asciiX - px(6) + editor.asciiScrollX) / cellWidth));
                const rowCells = characters[row];
                return rowCells[index]?.start ?? (row === lines.length - 1 ? shown.length : rowCells[rowCells.length - 1]?.start ?? line.start);
            }
            const at = indexInLine(shown, line, x - inner.x - gutterWidth + editor.scrollX, size);
            return bytes ? skipSeparators(shown, at) : at;
        };

        // ── Pointer: place, drag, double- and triple-click ─────────────────
        const touch = pointer.type === 'touch';
        // A mouse acts on press (and drags a selection); a touch on a tap (the
        // release), so a finger that starts a scroll does not open the keyboard.
        const overScrollbar = viewport && (viewport.before || viewport.after || lines.length * lineHeight > inner.height) &&
            pointer.x >= inner.x + inner.width - px(12);
        if (overScrollbar && ctx.focus === key) ctx.textFocusThumb = ctx.fullId(scrollId + ':thumb');
        const pressedHere = !pinching && !overScrollbar &&
            (touch ? state.clicked && ctx.tapped && !state.action : pointer.pressed && state.hovered);
        if (pressedHere && !composing) {
            editor.parked = false;
            const ascii = !!bytes && pointer.x >= asciiX;
            if (editor.ascii !== ascii) {
                editor.ascii = ascii;
                if (ctx.focus === key) {
                    sys.input.stopTextInput();
                    ctx.focus = null;
                }
            }
            const now = input.totalTime || 0, last = editor.lastPress;
            const near = Math.abs(pointer.x - last.x) < px(5) && Math.abs(pointer.y - last.y) < px(5);
            const count = now - last.time < 0.4 && near ? Math.min(3, last.count + 1) : 1;
            editor.lastPress = { time: now, x: pointer.x, y: pointer.y, count };
            const at = indexAt(pointer.x, pointer.y);
            if (count === 2 && !ascii) {
                const word = wordAt(model.value, at);
                editor.anchor = editor.start = word.start;
                editor.end = word.end;
            } else if (count === 3 && !ascii) {
                const line = lines[lineOf(lines, at)];
                editor.anchor = editor.start = line.start;
                editor.end = line.end;
            } else if (input.keyboard?.shift && ctx.focus === key) {
                editor.start = Math.min(editor.anchor, at);
                editor.end = Math.max(editor.anchor, at);
            } else editor.anchor = editor.start = editor.end = at;
            editor.goalX = null;
            editor.dragging = !touch && count === 1;
            startSession(key, editor, model);
        }
        if (editor.dragging) {
            if (pointer.down && ctx.active === key) {
                // Extend to the pointer; beyond the edges, the caret reveal scrolls.
                const y = clamp(pointer.y, inner.y - lineHeight, inner.y + inner.height + lineHeight);
                const at = indexAt(pointer.x, y);
                editor.start = Math.min(editor.anchor, at);
                editor.end = Math.max(editor.anchor, at);
                ctx.animating = true;
            } else editor.dragging = false;
        }

        // ── Keyboard ───────────────────────────────────────────────────────
        const focused = ctx.focus === key;
        if (focused) ctx.arrowsTaken = true;
        const native = !!input.nativeTextEditing;
        const hexInput = bytes && !editor.ascii;
        const snapCaret = () => {
            if (hexInput && editor.start === editor.end) {
                const from = editor.start;
                editor.start = editor.end = editor.anchor = skipSeparators(model.value, from);
                if (editor.lastEdit?.caret === from) editor.lastEdit.caret = editor.start;
            }
        };
        snapCaret();
        const pageMove = resetScroll ? viewport?.anchor.pageMove : undefined;
        let centerPage = false;
        let pageReload = false;
        if (focused && !composing && !pressedHere && (pageMove || !(resetScroll && viewport?.anchor.continueScroll))) {
            const text = model.value;
            const previousStart = editor.start, previousEnd = editor.end;
            const shift = !pageMove && !!input.keyboard?.shift;
            const word = !!(input.keyboard?.alt || input.keyboard?.ctrl);
            const selectionCaret = editor.start === editor.anchor ? editor.end : editor.start;
            const caret = hexInput ? skipSeparators(text, selectionCaret) : selectionCaret;
            /** Move the caret (extending the selection with Shift). */
            const moveTo = (at, keepGoal = false, direction = 1, hexDigitStep = false) => {
                at = clamp(at, 0, model.value.length);
                if (hexInput) at = skipSeparators(text, at, direction);
                if (shift) {
                    // A digit selection can end just before a separator even though the cursor skips it.
                    if (hexDigitStep && hexInput) {
                        while (at > editor.anchor && isSeparator(text[at - 1])) at--;
                    }
                    editor.start = Math.min(editor.anchor, at);
                    editor.end = Math.max(editor.anchor, at);
                } else editor.anchor = editor.start = editor.end = at;
                if (!keepGoal) editor.goalX = null;
            };
            /** Vertical moves keep the column the caret started from. */
            const moveLines = (count, paging = false) => {
                const cell = editor.ascii ? cells[byteAt(cells, caret)] : null;
                const index = lineOf(lines, cell?.start ?? caret);
                const line = lines[index];
                if (editor.goalX === null) editor.goalX = editor.ascii
                    ? (cell ? characters[index].indexOf(cell) : characters[index].length)
                    : measure(text.slice(line.start, caret), size);
                const target = index + count;
                if (paging && !shift && viewport &&
                    ((target < 0 && viewport.before) || (target >= lines.length && viewport.after))) {
                    const boundary = target < 0 ? 0 : lines.length - 1;
                    pageReload = viewport.edge(lines[boundary].start, Math.sign(count), 0,
                        { rows: target - boundary, column: editor.goalX });
                    return;
                }
                centerPage = paging;
                if (target < 0) return moveTo(0, true);
                if (target >= lines.length) return moveTo(text.length, true);
                if (editor.ascii) {
                    const row = characters[target];
                    return moveTo(row[Math.min(editor.goalX, row.length - 1)]?.start ?? lines[target].start, true);
                }
                moveTo(indexInLine(text, lines[target], editor.goalX, size), true);
            };
            const edit = (kind, change) => {
                if (readOnly) return;
                remember(editor, model, kind);
                change();
            };
            const deleteRange = (from, to) => {
                editor.start = Math.min(from, to);
                editor.end = Math.max(from, to);
                replaceSelection(editor, model, '');
            };

            const asciiInput = editor.ascii && bytes;
            if (pageMove) {
                editor.goalX = pageMove.column;
                moveLines(pageMove.rows, true);
            } else if (asciiInput && !readOnly && (input.textEdit?.text || (input.text && !command()))) {
                editAscii(asciiInput, editor, model, input.textEdit?.text || input.text, options);
            } else if (!asciiInput && native && input.textEdit && !readOnly) {
                model.value = options.maxLength ? input.textEdit.text.slice(0, options.maxLength) : input.textEdit.text;
                editor.start = clamp(input.textEdit.selectionStart, 0, model.value.length);
                editor.end = clamp(input.textEdit.selectionEnd, 0, model.value.length);
                editor.anchor = editor.start;
            } else if (!asciiInput && input.text && !readOnly && !command()) {
                edit('type', () => replaceSelection(editor, model, input.text, options.maxLength));
            } else if (command() && pressedOnce(KEY_A)) {
                editor.anchor = editor.start = 0;
                editor.end = text.length;
            } else if ((!native || asciiInput) && command() && (pressedOnce(KEY_C) || pressedOnce(KEY_X))) {
                const selected = asciiInput ? cells.filter(cell => cell.end > editor.start && cell.start < editor.end)
                    .map(cell => cell.char).join('') : text.slice(editor.start, editor.end);
                if (selected && typeof sys.device?.setClipboardText === 'function') sys.device.setClipboardText(selected);
                if (selected && pressedOnce(KEY_X) && !readOnly) {
                    if (asciiInput) editAscii(asciiInput, editor, model, '', options);
                    else edit('other', () => replaceSelection(editor, model, ''));
                }
            } else if (!native && command() && pressedOnce(KEY_V)) {
                const pasted = typeof sys.device?.getClipboardText === 'function' ? sys.device.getClipboardText() : null;
                if (pasted && !readOnly) {
                    if (asciiInput) editAscii(asciiInput, editor, model, pasted, options);
                    else edit('other', () => replaceSelection(editor, model, pasted.replace(/\r\n?/g, '\n'), options.maxLength));
                }
            } else if ((!native || asciiInput) && command() && (pressedOnce(KEY_Z) || pressedOnce(KEY_Y)) && !readOnly) {
                const redo = pressedOnce(KEY_Y) || shift;
                const from = redo ? editor.redo : editor.undo, to = redo ? editor.undo : editor.redo;
                const snapshot = from.pop();
                if (snapshot) {
                    to.push({ value: model.value, start: editor.start, end: editor.end });
                    model.value = snapshot.value;
                    editor.anchor = editor.start = snapshot.start;
                    editor.end = snapshot.end;
                    editor.lastEdit = null;
                }
            } else if (!native && repeated(KEY_ENTER)) {
                if (asciiInput && !readOnly) editAscii(asciiInput, editor, model, '\n', options);
                else edit('other', () => replaceSelection(editor, model, '\n', options.maxLength));
            } else if ((!native || asciiInput) && repeated(KEY_BACKSPACE)) {
                if (asciiInput && !readOnly) editAscii(asciiInput, editor, model, '', options, -1);
                else edit('delete', () => {
                    if (editor.start !== editor.end) replaceSelection(editor, model, '');
                    else if (hexInput && !word) {
                        const at = skipSeparators(text, stepLeft(text, editor.start), -1);
                        if (at < editor.start) deleteRange(at, stepRight(text, at));
                    } else deleteRange(word ? wordLeft(text, editor.start) : stepLeft(text, editor.start), editor.start);
                });
            } else if ((!native || asciiInput) && repeated(KEY_DELETE)) {
                if (asciiInput && !readOnly) editAscii(asciiInput, editor, model, '', options, 1);
                else edit('forward', () => editor.start !== editor.end ? replaceSelection(editor, model, '')
                    : deleteRange(editor.start, word ? wordRight(text, editor.start) : stepRight(text, editor.start)));
            } else if (repeated(KEY_LEFT)) {
                if (input.keyboard?.meta) moveTo(lines[lineOf(lines, caret)].start);
                else if (!shift && editor.start !== editor.end) moveTo(editor.start);
                else if (asciiInput) moveTo(cells[Math.max(0, byteAt(cells, caret) - 1)]?.start ?? 0);
                else moveTo(word ? wordLeft(text, caret) : stepLeft(text, caret), false, -1, !word);
            } else if (repeated(KEY_RIGHT)) {
                if (input.keyboard?.meta) moveTo(lines[lineOf(lines, caret)].end, false, -1);
                else if (!shift && editor.start !== editor.end) moveTo(editor.end);
                else if (asciiInput) moveTo(cells[byteAt(cells, caret) + 1]?.start ?? text.length);
                else moveTo(word ? wordRight(text, caret) : stepRight(text, caret), false, 1, !word);
            } else if (repeated(KEY_UP)) {
                if (input.keyboard?.meta) moveTo(0);
                else moveLines(-1);
            } else if (repeated(KEY_DOWN)) {
                if (input.keyboard?.meta) moveTo(text.length);
                else moveLines(1);
            } else if (pressedOnce(KEY_HOME)) {
                moveTo(command() ? 0 : lines[lineOf(lines, caret)].start);
            } else if (pressedOnce(KEY_END)) {
                moveTo(command() ? text.length : lines[lineOf(lines, caret)].end, false, -1);
            } else {
                const pageDirection = repeated(KEY_PAGE_UP) ? -1 : repeated(KEY_PAGE_DOWN) ? 1 : 0;
                if (pageDirection) moveLines(pageDirection * Math.max(1, Math.floor(inner.height / lineHeight) - 1), true);
            }
            editor.start = clamp(editor.start, 0, model.value.length);
            editor.end = clamp(editor.end, 0, model.value.length);
            editor.anchor = clamp(editor.anchor, 0, model.value.length);
            snapCaret();
            if (model.value !== text || editor.start !== previousStart || editor.end !== previousEnd) editor.parked = false;
        }
        if (focused && !editor.ascii && input.composition?.changed) {
            if (input.composition.active) editor.parked = false;
            editor.composition = input.composition.active ? input.composition.text : '';
            editor.compositionCaret = input.composition.selectionEnd;
        }
        // ── Drawing ────────────────────────────────────────────────────────
        const ring = ctx.springAt(key + '#focus', focused ? 1 : 0, springs.snappy);
        ctx.fill(rect, theme.surface);
        ctx.outline(rect, ctx.colorAt(key + '#ring', focused ? theme.focus : state.hovered ? theme.muted : theme.border), 1 + ring);
        const drawnText = focused && !editor.ascii && editor.composition
            ? model.value.slice(0, editor.start) + editor.composition + model.value.slice(editor.end) : model.value;
        if (drawnText !== shown) {
            lines = layoutContent(drawnText);
            if (bytes) {
                cells = bytes.scan(drawnText);
                characters = byteRows(lines, cells);
            }
        }
        const selectionCaret = editor.start === editor.anchor ? editor.end : editor.start;
        const caretIndex = focused && !editor.ascii && editor.composition ? editor.start + editor.compositionCaret
            : hexInput ? skipSeparators(drawnText, selectionCaret) : selectionCaret;
        const activeByte = bytes ? cells[byteAt(cells, caretIndex)] : null;
        const caretLine = lineOf(lines, caretIndex);
        const asciiRow = bytes ? lineOf(lines, activeByte?.start ?? caretIndex) : caretLine;
        const pageOffset = centerPage ? centeredOffset(editor.ascii ? asciiRow : caretLine) : undefined;
        if (viewport && pageOffset !== undefined && pageMove?.rows !== 0 && (pageMove || !input.keyboard?.shift)) {
            const limit = Math.max(0, lines.length * lineHeight - inner.height);
            const direction = pageOffset < 0 && viewport.before ? -1 : pageOffset > limit && viewport.after ? 1 : 0;
            // The caret can fit in this buffer while the centered viewport needs adjacent rows.
            if (direction) pageReload = viewport.edge(caretIndex, direction, 0, { rows: 0, column: editor.goalX });
        }
        const asciiColumn = bytes ? (activeByte ? characters[asciiRow].indexOf(activeByte) : characters[asciiRow].length) : 0;
        const showCursor = focused && input.focused && !readOnly && !editor.parked;
        const line = lines[caretLine];
        const caretColumn = measure(drawnText.slice(line.start, caretIndex), size);
        const next = Math.min(line.end, stepRight(drawnText, caretIndex));
        const advance = measure(drawnText.slice(line.start, next), size) - caretColumn;
        const cursorWidth = advance > 0 ? advance : measure('0', size);
        const viewportWidth = Math.max(1, textWidth - (!bytes && !viewport && lines.length * lineHeight > inner.height ? px(12) : 0));
        const revealCaret = focused && !editor.parked && (!resetScroll || pageMove) && !horizontalWheel && (caretIndex !== editor.lastCaret || editor.lastSize !== size ||
            editor.lastWrap !== wrap || editor.lastWidth !== textWidth || editor.lastAscii !== editor.ascii || drawnText !== shown);
        if (!wrap) {
            let widest = 0;
            for (const row of lines)
                widest = Math.max(widest, measure(drawnText.slice(row.start, row.end), size));
            const maxScroll = Math.max(0, widest + measure('0', size) - viewportWidth);
            if (revealCaret) {
                if (caretColumn < editor.scrollX) editor.scrollX = caretColumn;
                if (caretColumn + cursorWidth > editor.scrollX + viewportWidth)
                    editor.scrollX = caretColumn + cursorWidth - viewportWidth;
            }
            editor.scrollX = clamp(editor.scrollX, 0, maxScroll);
            let maxAsciiScroll = 0, asciiViewport = 0;
            if (bytes) {
                asciiViewport = Math.max(1, asciiWidth - px(6));
                const longest = characters.reduce((max, row) => Math.max(max, row.length), 0);
                maxAsciiScroll = Math.max(0, (longest + 1) * cellWidth - asciiViewport);
                if (revealCaret) {
                    const x = asciiColumn * cellWidth;
                    if (x < editor.asciiScrollX) editor.asciiScrollX = x;
                    if (x + cellWidth > editor.asciiScrollX + asciiViewport)
                        editor.asciiScrollX = x + cellWidth - asciiViewport;
                }
                editor.asciiScrollX = clamp(editor.asciiScrollX, 0, maxAsciiScroll);
            }
            const range = Math.max(maxScroll, maxAsciiScroll);
            if (range > 0) {
                const hexDominates = maxScroll >= maxAsciiScroll;
                const previous = hexDominates ? editor.scrollX : editor.asciiScrollX;
                const viewport = hexDominates ? viewportWidth : asciiViewport;
                const value = ui.horizontalScrollbar(id + ':horizontal', {
                    x: inner.x + gutterWidth, y: inner.y + inner.height, width: bytes ? columnsWidth : viewportWidth, height: scrollBarHeight,
                }, previous, viewport, viewport + range, { hoverArea: inner });
                if (value !== previous) {
                    editor.scrollX = value / range * maxScroll;
                    editor.asciiScrollX = value / range * maxAsciiScroll;
                }
            }
        }
        /** @type {Rect | null} */
        let caretRect = null;
        const rowAt = y => clamp(Math.floor(y / lineHeight), 0, lines.length - 1);
        if (focused && !readOnly && !editor.parked && !editor.composition) viewport?.reportCaret?.(caretIndex);
        const documentScroll = viewport && (viewport.before || viewport.after) ? {
            before: viewport.before && !composing,
            after: viewport.after && !composing,
            visible: Math.max(1, viewport.byteAt(lines[rowAt(offset + inner.height)].end) - viewport.byteAt(lines[rowAt(offset)].start)),
            offset: viewport.byteAt(lines[rowAt(offset)].start),
            size: viewport.size,
            seek: composing ? () => {} : viewport.seek,
            edge: (y, delta) => {
                if (centerPage || pageMove || pageReload) return;
                // Anchor the new buffer to a visible row, retaining the fractional scroll.
                const travel = clamp(y + delta, 0, Math.max(0, lines.length * lineHeight - 1));
                const row = rowAt(travel);
                viewport.edge(lines[row].start, delta, travel - row * lineHeight + Math.min(0, y + delta));
            },
        } : undefined;
        ui.scroll(scrollId, inner, Math.max(lines.length * lineHeight, inner.height), content => {
            const first = clamp(Math.floor((inner.y - content.y) / lineHeight), 0, lines.length - 1);
            const last = clamp(Math.ceil((inner.y + inner.height - content.y) / lineHeight), 0, lines.length - 1);
            if (viewport) viewport.report(lines[first].start);
            const textLeft = content.x + gutterWidth;
            const textX = textLeft - editor.scrollX;
            const margin = { ...inner, width: gutterWidth };
            const caretY = content.y + caretLine * lineHeight;
            const hexWidth = Math.min(textWidth, content.width - gutterWidth);
            const asciiLeft = textLeft + hexWidth;
            const asciiRight = asciiLeft + asciiWidth;
            if (editor.parked) {
                // A new edit can start at a visible row without changing the saved offscreen caret just by scrolling.
                editor.start = editor.end = editor.anchor = lines[first].start;
            }
            if (gutterWidth > 0) ctx.fill(margin, theme.raised, 0);
            if (showCursor) {
                for (let i = first; i <= last; i++) {
                    const y = content.y + i * lineHeight;
                    if (lines[i].logicalStart === line.logicalStart)
                        ctx.fill({ x: content.x, y, width: gutterWidth + hexWidth, height: lineHeight },
                            withAlpha(theme.accent, editor.ascii ? 0.08 : bytes ? 0.18 : 0.13), 0);
                    if (bytes && lines[i].logicalStart === lines[asciiRow].logicalStart)
                        ctx.fill({ x: asciiLeft, y, width: asciiWidth, height: lineHeight },
                        withAlpha(theme.accent, editor.ascii ? 0.18 : 0.08), 0);
                }
            }
            if (gutter && gutterWidth > 0) {
                const clip = ctx.pushClip(margin);
                const labels = gutter.labels(drawnText, lines.slice(first, last + 1).map(line => line.start));
                for (let i = first; i <= last; i++) {
                    const label = labels[i - first];
                    const baseline = content.y + i * lineHeight + lineHeight / 2 + gutterSize * 0.35;
                    ctx.text(label, textLeft - px(10) - measure(label, gutterSize), baseline, gutterSize, theme.muted);
                }
                ctx.popClip(clip);
                ctx.line(textLeft - px(5), inner.y, textLeft - px(5), inner.y + inner.height, theme.border);
            }
            const textClip = ctx.pushClip({ ...inner, x: textLeft, width: Math.max(0, hexWidth) });
            const selecting = focused && editor.end > editor.start && !editor.composition;
            for (let i = first; i <= last; i++) {
                const line = lines[i], y = content.y + i * lineHeight;
                if (selecting && editor.start <= line.end && editor.end >= line.start) {
                    const from = Math.max(editor.start, line.start), to = Math.min(editor.end, line.end);
                    const x0 = textX + measure(drawnText.slice(line.start, from), size);
                    // A selected line break shows as a sliver past the line end.
                    const tail = editor.end > line.end && drawnText[line.end] === '\n' ? size * 0.3 : 0;
                    const x1 = textX + measure(drawnText.slice(line.start, to), size) + tail;
                    ctx.fill({ x: x0, y, width: Math.max(0, x1 - x0), height: lineHeight },
                        withAlpha(theme.accent, editor.ascii ? 0.12 : 0.22), px(2));
                }
                ctx.text(drawnText.slice(line.start, line.end).replace(/\s+$/, ''), textX, y + lineHeight / 2 + size * 0.35, size, theme.ink);
            }
            if (!drawnText && options.placeholder)
                ctx.text(options.placeholder, textX, content.y + lineHeight / 2 + size * 0.35, size, theme.muted);
            const caretX = textX + caretColumn;
            caretRect = editor.parked
                ? { x: editor.ascii ? asciiLeft + px(6) : textX, y: content.y + first * lineHeight, width: 1, height: lineHeight }
                : { x: caretX, y: caretY, width: 1, height: lineHeight };
            if (focused && editor.composition) {
                const from = editor.start, to = editor.start + editor.composition.length;
                for (let i = lineOf(lines, from); i <= lineOf(lines, to) && i < lines.length; i++) {
                    const l = lines[i], y = content.y + (i + 1) * lineHeight - px(4);
                    const x0 = textX + measure(drawnText.slice(l.start, Math.max(from, l.start)), size);
                    const x1 = textX + measure(drawnText.slice(l.start, Math.min(to, l.end)), size);
                    ctx.line(x0, y, x1, y, theme.highlight);
                }
            }
            if (showCursor) {
                const start = editor.ascii && activeByte ? clamp(activeByte.start, line.start, line.end) : caretIndex;
                const end = editor.ascii && activeByte ? Math.min(line.end, activeByte.end) : next;
                const blockX = textX + measure(drawnText.slice(line.start, start), size);
                const width = Math.min(editor.ascii && end > start
                    ? textX + measure(drawnText.slice(line.start, end), size) - blockX : cursorWidth, hexWidth);
                if (width > 0) {
                    const x = wrap ? clamp(blockX, textLeft, textLeft + hexWidth - width) : blockX;
                    ctx.outline({ x, y: caretY + px(1), width, height: lineHeight - px(2) },
                        editor.ascii ? withAlpha(theme.focus, 0.55) : theme.focus, px(1), 0);
                }
            }
            ctx.popClip(textClip);
            if (bytes) {
                ctx.line(asciiLeft, inner.y, asciiLeft, inner.y + inner.height, theme.border);
                const clip = ctx.pushClip({ ...inner, x: asciiLeft, width: Math.max(0, asciiRight - asciiLeft) });
                const origin = asciiLeft + px(6) - editor.asciiScrollX;
                for (let i = first; i <= last; i++) {
                    const row = characters[i], y = content.y + i * lineHeight;
                    for (let j = 0; selecting && j < row.length; j++) {
                        if (row[j].end <= editor.start || row[j].start >= editor.end) continue;
                        ctx.fill({ x: origin + j * cellWidth, y, width: cellWidth, height: lineHeight },
                            withAlpha(theme.accent, editor.ascii ? 0.22 : 0.12), 0);
                    }
                    ctx.text(row.map(cell => cell.char).join(''), origin, y + lineHeight / 2 + size * 0.35, size, theme.ink);
                    if (showCursor && i === asciiRow) {
                        const x = wrap ? Math.min(origin + asciiColumn * cellWidth, asciiRight - cellWidth)
                            : origin + asciiColumn * cellWidth;
                        ctx.outline({ x, y: y + px(1), width: cellWidth, height: lineHeight - px(2) },
                            editor.ascii ? theme.focus : withAlpha(theme.focus, 0.55), px(1), 0);
                        if (editor.ascii) caretRect = { x, y, width: 1, height: lineHeight };
                    }
                }
                ctx.popClip(clip);
            }
        }, { radius: 0, reset: resetScroll, initialOffset, targetOffset: pageReload ? offset : pageOffset,
            document: documentScroll, touchScroll: !pinching,
            wheelScroll: !zoomWheel && !(horizontalWheel && input.keyboard.shift) });
        // Keep the caret in view after it moves (applied on the next frame).
        if (focused && caretRect && ((revealCaret && !centerPage && !pageReload) || editor.dragging)) {
            ctx.reveal = { scroll: scrollKey, rect: ctx.toScreen(caretRect) };
            ctx.animating = true;
        }
        editor.lastCaret = caretIndex;
        editor.lastSize = size;
        editor.lastWrap = wrap;
        editor.lastWidth = textWidth;
        editor.lastAscii = editor.ascii;
        if (font !== null) {
            // The rest of the interface measures and draws with the default font.
            sys.canvas.setFont(null);
            font = null;
        }
        if (focused && caretRect) sys.input.updateTextInput({
            text: editor.ascii ? '' : model.value, selectionStart: editor.ascii ? 0 : editor.start,
            selectionEnd: editor.ascii ? 0 : editor.end,
            caret: { x: caretRect.x, y: caretRect.y, width: 1, height: caretRect.height },
        });
        return model.value;
    }

    Object.assign(ui, { textArea });
}
