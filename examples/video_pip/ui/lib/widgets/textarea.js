/** Multiline text: wrapped lines, drag and double-click selection, word and
 *  line navigation, held-key repeat, clipboard, undo and redo, scrolling that
 *  follows the caret. Text arrives through Budo text sessions (IME, soft
 *  keyboards); where the platform edits natively (web, Android), it owns the
 *  text changes and undo, and the widget keeps navigation and selection. */

import { clamp } from '../math.js';
import { springs } from '../motion.js';
import { withAlpha } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {{ value: string }} TextModel */
/** @typedef {{ placeholder?: string, label?: string, maxLength?: number, readOnly?: boolean,
 *   size?: number, font?: string }} TextAreaOptions  `font`: a name loaded with sys.font.load */
/** @typedef {{ start: number, end: number }} Line  UTF-16 range of a visual line, without its newline */
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

    /** Visual lines of the whole text. @returns {Line[]} */
    function layout(text, width, size) {
        const lines = [];
        let offset = 0;
        for (const paragraph of text.split('\n')) {
            for (const line of wrapParagraph(paragraph, width, size))
                lines.push({ start: offset + line.start, end: offset + line.end });
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
        let previous = 0;
        for (let index = line.start; index < line.end;) {
            const next = index + (/[\uD800-\uDBFF]/.test(text[index]) ? 2 : 1);
            const width = measure(text.slice(line.start, next), size);
            if (x < (previous + width) / 2) return index;
            previous = width;
            index = next;
        }
        // Past the end of a soft-wrapped line, stay before its trailing break.
        return line.end;
    }

    // ── Editing helpers ────────────────────────────────────────────────────

    const isLow = c => /[\uDC00-\uDFFF]/.test(c || '');
    const isHigh = c => /[\uD800-\uDBFF]/.test(c || '');
    const stepLeft = (text, at) => at > 0 ? at - (isLow(text[at - 1]) ? 2 : 1) : 0;
    const stepRight = (text, at) => at < text.length ? at + (isHigh(text[at]) ? 2 : 1) : text.length;
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
                composition: '', compositionCaret: 0, blinkStarted: ctx.input.totalTime || 0,
                goalX: null, undo: [], redo: [], lastEdit: null, lastPress: { time: -1, x: 0, y: 0, count: 0 },
                lastCaret: -1, dragging: false,
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
        sys.input.startTextInput({ text: model.value, selectionStart: editor.start, selectionEnd: editor.end, multiline: true });
    }

    // ── The widget ─────────────────────────────────────────────────────────

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
        const size = options.size || ctx.sp(theme.font), lineHeight = Math.round(size * 1.4);
        font = null;
        if (options.font && typeof sys.canvas.setFont === 'function') {
            try {
                sys.canvas.setFont(options.font);
                font = options.font;
            } catch (e) { /* unknown font: keep the default */ }
        }
        const readOnly = !!options.readOnly;
        const editor = editorFor(key, model);
        const pointer = input.pointer;
        const inner = { x: rect.x + px(12), y: rect.y + px(10), width: rect.width - px(24), height: rect.height - px(20) };
        const scrollKey = ctx.fullId(id + ':scroll');

        if (state.action?.action === 'setValue' && !readOnly) {
            remember(editor, model, 'other');
            model.value = options.maxLength ? state.action.value.slice(0, options.maxLength) : state.action.value;
            editor.start = editor.end = editor.anchor = model.value.length;
        }
        if (state.action) startSession(key, editor, model);
        // Keyboard focus arrived (Tab): edit with the caret where it was.
        else if (state.focused && ctx.focus !== key && ctx.focusVisible) startSession(key, editor, model);
        // In a scrolling page, stay in view while the on-screen keyboard opens.
        if (ctx.focus === key) ctx.revealAboveKeyboard(rect);

        // Wrap the shown text (with an IME composition inside) to the width
        // left after the scrollbar gutter, then decide where the scrollbar goes.
        const composing = ctx.focus === key && !!editor.composition;
        const shown = composing
            ? model.value.slice(0, editor.start) + editor.composition + model.value.slice(editor.end) : model.value;
        let lines = layout(shown, inner.width, size);
        if (lines.length * lineHeight > inner.height) lines = layout(shown, inner.width - px(12), size);
        const contentHeight = lines.length * lineHeight;
        const offset = ctx.motion.get('n:' + scrollKey + '#offset')?.spring.value || 0;

        /** Text index at a canvas point (relative to the scrolled content). */
        const indexAt = (x, y) => {
            const line = lines[clamp(Math.floor((y - inner.y + offset) / lineHeight), 0, lines.length - 1)];
            return indexInLine(shown, line, x - inner.x, size);
        };

        // ── Pointer: place, drag, double- and triple-click ─────────────────
        const previous = { start: editor.start, end: editor.end, text: model.value, caret: editor.compositionCaret };
        const touch = pointer.type === 'touch';
        // A mouse acts on press (and drags a selection); a touch on a tap (the
        // release), so a finger that starts a scroll does not open the keyboard.
        const pressedHere = touch ? state.clicked && ctx.tapped && !state.action : pointer.pressed && state.hovered;
        if (pressedHere && !composing) {
            const now = input.totalTime || 0, last = editor.lastPress;
            const near = Math.abs(pointer.x - last.x) < px(5) && Math.abs(pointer.y - last.y) < px(5);
            const count = now - last.time < 0.4 && near ? Math.min(3, last.count + 1) : 1;
            editor.lastPress = { time: now, x: pointer.x, y: pointer.y, count };
            const at = indexAt(pointer.x, pointer.y);
            if (count === 2) {
                const word = wordAt(model.value, at);
                editor.anchor = editor.start = word.start;
                editor.end = word.end;
            } else if (count === 3) {
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
        if (focused && !composing && !pressedHere) {
            const text = model.value;
            const shift = !!input.keyboard?.shift;
            const word = !!(input.keyboard?.alt || input.keyboard?.ctrl);
            const caret = editor.start === editor.anchor ? editor.end : editor.start;
            /** Move the caret (extending the selection with Shift). */
            const moveTo = (at, keepGoal = false) => {
                at = clamp(at, 0, model.value.length);
                if (shift) {
                    editor.start = Math.min(editor.anchor, at);
                    editor.end = Math.max(editor.anchor, at);
                } else editor.anchor = editor.start = editor.end = at;
                if (!keepGoal) editor.goalX = null;
            };
            /** Vertical moves keep the column the caret started from. */
            const moveLines = count => {
                const index = lineOf(lines, caret);
                const line = lines[index];
                if (editor.goalX === null) editor.goalX = measure(text.slice(line.start, caret), size);
                const target = index + count;
                if (target < 0) return moveTo(0, true);
                if (target >= lines.length) return moveTo(text.length, true);
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

            if (native && input.textEdit && !readOnly) {
                model.value = options.maxLength ? input.textEdit.text.slice(0, options.maxLength) : input.textEdit.text;
                editor.start = clamp(input.textEdit.selectionStart, 0, model.value.length);
                editor.end = clamp(input.textEdit.selectionEnd, 0, model.value.length);
                editor.anchor = editor.start;
            } else if (input.text && !readOnly && !command()) {
                edit('type', () => replaceSelection(editor, model, input.text, options.maxLength));
            } else if (command() && pressedOnce(KEY_A)) {
                editor.anchor = editor.start = 0;
                editor.end = text.length;
            } else if (!native && command() && (pressedOnce(KEY_C) || pressedOnce(KEY_X))) {
                const selected = text.slice(editor.start, editor.end);
                if (selected && typeof sys.device?.setClipboardText === 'function') sys.device.setClipboardText(selected);
                if (selected && pressedOnce(KEY_X)) edit('other', () => replaceSelection(editor, model, ''));
            } else if (!native && command() && pressedOnce(KEY_V)) {
                const pasted = typeof sys.device?.getClipboardText === 'function' ? sys.device.getClipboardText() : null;
                if (pasted) edit('other', () => replaceSelection(editor, model, pasted.replace(/\r\n?/g, '\n'), options.maxLength));
            } else if (!native && command() && (pressedOnce(KEY_Z) || pressedOnce(KEY_Y))) {
                const redo = pressedOnce(KEY_Y) || shift;
                const from = redo ? editor.redo : editor.undo, to = redo ? editor.undo : editor.redo;
                const snapshot = from.pop();
                if (snapshot && !readOnly) {
                    to.push({ value: model.value, start: editor.start, end: editor.end });
                    model.value = snapshot.value;
                    editor.anchor = editor.start = snapshot.start;
                    editor.end = snapshot.end;
                    editor.lastEdit = null;
                }
            } else if (!native && repeated(KEY_ENTER)) {
                edit('other', () => replaceSelection(editor, model, '\n', options.maxLength));
            } else if (!native && repeated(KEY_BACKSPACE)) {
                edit('delete', () => editor.start !== editor.end ? replaceSelection(editor, model, '')
                    : deleteRange(word ? wordLeft(text, editor.start) : stepLeft(text, editor.start), editor.start));
            } else if (!native && repeated(KEY_DELETE)) {
                edit('forward', () => editor.start !== editor.end ? replaceSelection(editor, model, '')
                    : deleteRange(editor.start, word ? wordRight(text, editor.start) : stepRight(text, editor.start)));
            } else if (repeated(KEY_LEFT)) {
                if (input.keyboard?.meta) moveTo(lines[lineOf(lines, caret)].start);
                else if (!shift && editor.start !== editor.end) moveTo(editor.start);
                else moveTo(word ? wordLeft(text, caret) : stepLeft(text, caret));
            } else if (repeated(KEY_RIGHT)) {
                if (input.keyboard?.meta) moveTo(lines[lineOf(lines, caret)].end);
                else if (!shift && editor.start !== editor.end) moveTo(editor.end);
                else moveTo(word ? wordRight(text, caret) : stepRight(text, caret));
            } else if (repeated(KEY_UP)) {
                if (input.keyboard?.meta) moveTo(0);
                else moveLines(-1);
            } else if (repeated(KEY_DOWN)) {
                if (input.keyboard?.meta) moveTo(text.length);
                else moveLines(1);
            } else if (pressedOnce(KEY_HOME)) {
                moveTo(command() ? 0 : lines[lineOf(lines, caret)].start);
            } else if (pressedOnce(KEY_END)) {
                moveTo(command() ? text.length : lines[lineOf(lines, caret)].end);
            } else if (repeated(KEY_PAGE_UP)) {
                moveLines(-Math.max(1, Math.floor(inner.height / lineHeight) - 1));
            } else if (repeated(KEY_PAGE_DOWN)) {
                moveLines(Math.max(1, Math.floor(inner.height / lineHeight) - 1));
            }
            editor.start = clamp(editor.start, 0, model.value.length);
            editor.end = clamp(editor.end, 0, model.value.length);
            editor.anchor = clamp(editor.anchor, 0, model.value.length);
        }
        if (focused && input.composition?.changed) {
            editor.composition = input.composition.active ? input.composition.text : '';
            editor.compositionCaret = input.composition.selectionEnd;
        }
        if (focused && (editor.start !== previous.start || editor.end !== previous.end ||
            model.value !== previous.text || editor.compositionCaret !== previous.caret))
            editor.blinkStarted = input.totalTime || 0;
        // The text changed this frame: wrap it again before drawing.
        if (model.value !== previous.text || composing !== (ctx.focus === key && !!editor.composition)) {
            const now = ctx.focus === key && editor.composition
                ? model.value.slice(0, editor.start) + editor.composition + model.value.slice(editor.end) : model.value;
            lines = layout(now, inner.width - (lines.length * lineHeight > inner.height ? px(12) : 0), size);
        }

        // ── Drawing ────────────────────────────────────────────────────────
        const ring = ctx.springAt(key + '#focus', focused ? 1 : 0, springs.snappy);
        ctx.fill(rect, theme.surface);
        ctx.outline(rect, ctx.colorAt(key + '#ring', focused ? theme.focus : state.hovered ? theme.muted : theme.border), 1 + ring);
        const drawnText = focused && editor.composition
            ? model.value.slice(0, editor.start) + editor.composition + model.value.slice(editor.end) : model.value;
        const caretIndex = focused && editor.composition ? editor.start + editor.compositionCaret
            : editor.start === editor.anchor ? editor.end : editor.start;
        const caretLine = lineOf(lines, caretIndex);
        let caretRect = null;
        ui.scroll(id + ':scroll', inner, Math.max(lines.length * lineHeight, inner.height), content => {
            const first = clamp(Math.floor((inner.y - content.y) / lineHeight), 0, lines.length - 1);
            const last = clamp(Math.ceil((inner.y + inner.height - content.y) / lineHeight), 0, lines.length - 1);
            const selecting = focused && editor.end > editor.start && !editor.composition;
            for (let i = first; i <= last; i++) {
                const line = lines[i], y = content.y + i * lineHeight;
                if (selecting && editor.start <= line.end && editor.end >= line.start) {
                    const from = Math.max(editor.start, line.start), to = Math.min(editor.end, line.end);
                    const x0 = content.x + measure(drawnText.slice(line.start, from), size);
                    // A selected line break shows as a sliver past the line end.
                    const tail = editor.end > line.end && drawnText[line.end] === '\n' ? size * 0.3 : 0;
                    const x1 = content.x + measure(drawnText.slice(line.start, to), size) + tail;
                    ctx.fill({ x: x0, y, width: Math.max(0, x1 - x0), height: lineHeight }, withAlpha(theme.accent, 0.22), px(2));
                }
                ctx.text(drawnText.slice(line.start, line.end).replace(/\s+$/, ''), content.x, y + lineHeight / 2 + size * 0.35, size, theme.ink);
            }
            if (!drawnText && options.placeholder)
                ctx.text(options.placeholder, content.x, content.y + lineHeight / 2 + size * 0.35, size, theme.muted);
            const line = lines[caretLine];
            const caretX = content.x + measure(drawnText.slice(line.start, caretIndex), size);
            caretRect = { x: caretX, y: content.y + caretLine * lineHeight, width: 1, height: lineHeight };
            if (focused && editor.composition) {
                const from = editor.start, to = editor.start + editor.composition.length;
                for (let i = lineOf(lines, from); i <= lineOf(lines, to) && i < lines.length; i++) {
                    const l = lines[i], y = content.y + (i + 1) * lineHeight - px(4);
                    const x0 = content.x + measure(drawnText.slice(l.start, Math.max(from, l.start)), size);
                    const x1 = content.x + measure(drawnText.slice(l.start, Math.min(to, l.end)), size);
                    ctx.line(x0, y, x1, y, theme.highlight);
                }
            }
            if (focused) {
                const blink = (input.totalTime || 0) - editor.blinkStarted;
                if (Math.floor(blink * 2) % 2 === 0)
                    ctx.line(caretX, caretRect.y + px(3), caretX, caretRect.y + lineHeight - px(3), theme.ink);
                ui.wakeAfter(0.5 - (blink % 0.5));
            }
        }, { radius: 0 });
        // Keep the caret in view after it moves (applied on the next frame).
        if (focused && caretRect && (caretIndex !== editor.lastCaret || editor.dragging)) {
            ctx.reveal = { scroll: scrollKey, rect: ctx.toScreen(caretRect) };
            ctx.animating = true;
        }
        editor.lastCaret = caretIndex;
        if (font !== null) {
            // The rest of the interface measures and draws with the default font.
            sys.canvas.setFont(null);
            font = null;
        }
        if (focused && caretRect) sys.input.updateTextInput({
            text: model.value, selectionStart: editor.start, selectionEnd: editor.end,
            caret: { x: caretRect.x, y: caretRect.y, width: 1, height: caretRect.height },
        });
        return model.value;
    }

    Object.assign(ui, { textArea });
}
