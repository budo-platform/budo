/** Text fields with Budo text sessions (IME composition, soft keyboards). */

import { clamp } from '../math.js';
import { springs } from '../motion.js';
import { withAlpha } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {{ value: string }} TextModel */
/** @typedef {{ placeholder?: string, maxLength?: number, label?: string }} FieldOptions */
/** @typedef {{ start: number, end: number, composition: string,
 *   compositionCaret: number, scrollX: number, blinkStarted: number }} EditorState */

// USB HID scancodes
const KEY_ENTER = 40, KEY_ESCAPE = 41, KEY_BACKSPACE = 42, KEY_RIGHT = 79, KEY_LEFT = 80;
const KEY_A = 4, KEY_C = 6, KEY_V = 25, KEY_X = 27;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;
    /** @type {Map<string, EditorState>} */
    const editors = new Map();
    /** @type {string | null} */
    let inlineEditId = null;
    const inlineDraft = { value: '' };

    /** Nearest UTF-16 index to a horizontal text position.
     *  @param {string} value @param {number} x @param {number} size @returns {number} */
    function textIndexAt(value, x, size) {
        if (x <= 0) return 0;
        let index = 0;
        let previousWidth = 0;
        for (const character of value) {
            const nextIndex = index + character.length;
            const nextWidth = ctx.measure(value.slice(0, nextIndex), size);
            if (x < (previousWidth + nextWidth) / 2) return index;
            index = nextIndex;
            previousWidth = nextWidth;
        }
        return value.length;
    }

    /** Ctrl (or Cmd) plus `scancode`, pressed this frame. @param {number} scancode */
    function shortcut(scancode) {
        const keyboard = ctx.input.keyboard;
        return !!keyboard && (keyboard.ctrl || keyboard.meta) && sys.input.isKeyPressed(scancode);
    }

    /** Give `id` the keyboard focus with the caret at the end. @param {string} id @param {TextModel} model */
    function focusField(id, model) {
        const key = ctx.fullId(id);
        if (ctx.focus !== null) sys.input.stopTextInput();
        ctx.focus = key;
        ctx.keyFocus = key;
        ctx.freshFocus = key;
        const editor = editors.get(key) || { start: 0, end: 0, composition: '', compositionCaret: 0, scrollX: 0, blinkStarted: 0 };
        editor.start = editor.end = model.value.length;
        editor.composition = '';
        editor.compositionCaret = 0;
        editor.blinkStarted = ctx.input.totalTime || 0;
        editors.set(key, editor);
        sys.input.startTextInput({ text: model.value, selectionStart: editor.end, selectionEnd: editor.end, multiline: false });
    }

    /** While the on-screen keyboard opens (and the view shrinks above it),
     *  scroll the focused field into view in its scroll area.
     *  @param {Rect} rect */
    function revealAboveKeyboard(rect) {
        if (!ctx.keyboardChanging || ctx.scrollStack.length === 0) return;
        ctx.reveal = { scroll: ctx.scrollStack[ctx.scrollStack.length - 1], rect: ctx.toScreen(rect) };
    }
    ctx.revealAboveKeyboard = revealAboveKeyboard;

    /** Single-line text field editing `model.value` in place.
     *  @param {string} id @param {Rect} rect @param {TextModel} model
     *  @param {FieldOptions} [options] @returns {string} the current value */
    function field(id, rect, model, options = {}) {
        const state = ctx.interact(id, rect, {
            cursor: 'text',
            a11y: { role: 'textbox', label: options.label || options.placeholder || '', value: model.value },
        });
        const key = state.id;
        const input = ctx.input;
        const theme = t();
        const px = ctx.px;
        const size = ctx.sp(theme.font);
        // Screen readers focus fields and set their text.
        if (state.action?.action === 'setValue') model.value = state.action.value.replace(/[\r\n]+/g, ' ');
        if (state.action && ctx.focus !== key) focusField(id, model);
        // Keyboard focus arrived (Tab): start editing with the caret at the end.
        else if (state.focused && ctx.focus !== key && ctx.focusVisible) focusField(id, model);
        const editor = editors.get(key) || {
            start: model.value.length, end: model.value.length,
            composition: '', compositionCaret: 0, scrollX: 0, blinkStarted: input.totalTime || 0
        };
        editors.set(key, editor);
        const inner = ui.inset(rect, px(12));
        // A mouse places the caret on press; a touch on a tap (the release),
        // so a finger that starts a scroll on a field scrolls and does not
        // open the keyboard.
        const placedCaret = input.pointer.type === 'touch'
            ? state.clicked && ctx.tapped && !state.action
            : input.pointer.pressed && state.hovered;
        const justFocused = ctx.freshFocus === key;
        if (justFocused) ctx.freshFocus = null;
        const previous = { start: editor.start, end: editor.end, text: model.value, caret: editor.compositionCaret };
        if (placedCaret) {
            const caret = textIndexAt(model.value, input.pointer.x - inner.x + editor.scrollX, size);
            editor.start = editor.end = caret;
            editor.composition = '';
            editor.compositionCaret = 0;
            if (ctx.focus !== key) {
                if (ctx.focus !== null) sys.input.stopTextInput();
                ctx.focus = key;
                sys.input.startTextInput({ text: model.value, selectionStart: caret, selectionEnd: caret, multiline: false });
            }
        }
        const focused = ctx.focus === key;
        if (focused) revealAboveKeyboard(rect);
        if (focused) {
            const editing = !placedCaret && !justFocused;
            if (editing && input.textEdit) {
                model.value = input.textEdit.text;
                editor.start = input.textEdit.selectionStart;
                editor.end = input.textEdit.selectionEnd;
            } else if (editing && input.text) {
                model.value = model.value.slice(0, editor.start) + input.text + model.value.slice(editor.end);
                editor.start = editor.end = editor.start + input.text.length;
            } else if (editing && !input.nativeTextEditing && shortcut(KEY_A)) {
                editor.start = 0;
                editor.end = model.value.length;
            } else if (editing && !input.nativeTextEditing && (shortcut(KEY_C) || shortcut(KEY_X))) {
                const selected = model.value.slice(editor.start, editor.end);
                if (selected && typeof sys.device?.setClipboardText === 'function') sys.device.setClipboardText(selected);
                if (selected && shortcut(KEY_X)) {
                    model.value = model.value.slice(0, editor.start) + model.value.slice(editor.end);
                    editor.end = editor.start;
                }
            } else if (editing && !input.nativeTextEditing && shortcut(KEY_V)) {
                const pasted = typeof sys.device?.getClipboardText === 'function' ? sys.device.getClipboardText() : null;
                if (pasted) {
                    const text = pasted.replace(/[\r\n]+/g, ' ');
                    model.value = model.value.slice(0, editor.start) + text + model.value.slice(editor.end);
                    editor.start = editor.end = editor.start + text.length;
                }
            } else if (editing && sys.input.isKeyPressed(KEY_BACKSPACE)) {
                const start = editor.start === editor.end ? Math.max(0, editor.start - 1) : editor.start;
                model.value = model.value.slice(0, start) + model.value.slice(editor.end);
                editor.start = editor.end = start;
            } else if (editing && (sys.input.isKeyPressed(KEY_LEFT) || sys.input.isKeyPressed(KEY_RIGHT))) {
                const left = sys.input.isKeyPressed(KEY_LEFT);
                /** @param {number} at */
                const step = at => left
                    ? (at > 0 ? at - (/[\uDC00-\uDFFF]/.test(model.value[at - 1]) ? 2 : 1) : at)
                    : (at < model.value.length ? at + (/[\uD800-\uDBFF]/.test(model.value[at]) ? 2 : 1) : at);
                if (input.keyboard?.shift) {
                    // Extend the selection from its anchor.
                    if (editor.start === editor.end || editor.anchor === undefined) editor.anchor = editor.start;
                    const caret = step(editor.anchor === editor.start ? editor.end : editor.start);
                    editor.start = Math.min(editor.anchor, caret);
                    editor.end = Math.max(editor.anchor, caret);
                } else {
                    let caret = left ? Math.min(editor.start, editor.end) : Math.max(editor.start, editor.end);
                    if (editor.start === editor.end) caret = step(caret);
                    editor.start = editor.end = caret;
                }
            }
            if (options.maxLength && model.value.length > options.maxLength)
                model.value = model.value.slice(0, options.maxLength);
            editor.start = clamp(editor.start, 0, model.value.length);
            editor.end = clamp(editor.end, 0, model.value.length);
            if (input.composition?.changed) {
                editor.composition = input.composition.active ? input.composition.text : '';
                editor.compositionCaret = input.composition.selectionEnd;
            }
            if (placedCaret || editor.start !== previous.start || editor.end !== previous.end ||
                model.value !== previous.text || editor.compositionCaret !== previous.caret)
                editor.blinkStarted = input.totalTime || 0;
        }
        const ring = ctx.springAt(key + '#focus', focused ? 1 : 0, springs.snappy);
        ctx.fill(rect, theme.surface);
        ctx.outline(rect, ctx.colorAt(key + '#ring', focused ? theme.focus : state.hovered ? theme.muted : theme.border),
            1 + ring);
        const prefix = model.value.slice(0, editor.start);
        const shown = editor.composition ? prefix + editor.composition + model.value.slice(editor.end) : model.value;
        const caretText = editor.composition
            ? prefix + editor.composition.slice(0, editor.compositionCaret)
            : model.value.slice(0, editor.end);
        const caretOffset = ctx.measure(caretText, size);
        const shownWidth = ctx.measure(shown, size);
        if (focused) {
            if (caretOffset < editor.scrollX) editor.scrollX = caretOffset;
            if (caretOffset > editor.scrollX + inner.width - 2) editor.scrollX = caretOffset - inner.width + 2;
        }
        editor.scrollX = clamp(editor.scrollX, 0, Math.max(0, shownWidth - inner.width + 2));
        const textX = inner.x - editor.scrollX;
        const clip = ctx.pushClip({ x: inner.x, y: rect.y, width: inner.width, height: rect.height });
        if (focused && editor.end > editor.start && !editor.composition) {
            const from = textX + ctx.measure(model.value.slice(0, editor.start), size);
            const to = textX + ctx.measure(model.value.slice(0, editor.end), size);
            ctx.fill({ x: from, y: rect.y + px(8), width: to - from, height: rect.height - px(16) },
                withAlpha(theme.accent, 0.22), px(2));
        }
        ctx.text(shown || options.placeholder || '', textX, rect.y + rect.height / 2 + size * 0.35, size,
            shown ? theme.ink : theme.muted);
        const caretX = textX + caretOffset;
        if (focused) {
            if (editor.composition) {
                const underlineX = textX + ctx.measure(prefix, size);
                ctx.line(underlineX, rect.y + rect.height - px(8),
                    underlineX + ctx.measure(editor.composition, size), rect.y + rect.height - px(8), theme.highlight);
            }
            const blink = (input.totalTime || 0) - editor.blinkStarted;
            if (Math.floor(blink * 2) % 2 === 0)
                ctx.line(caretX, rect.y + px(10), caretX, rect.y + rect.height - px(10), theme.ink);
            ui.wakeAfter(0.5 - (blink % 0.5));
        }
        ctx.popClip(clip);
        if (focused) sys.input.updateTextInput({
            text: model.value, selectionStart: editor.start, selectionEnd: editor.end,
            caret: { x: caretX, y: rect.y + px(5), width: 1, height: rect.height - px(10) }
        });
        return model.value;
    }

    /** A label that turns into a field when clicked. Returns the confirmed
     *  new value (Enter or OK), or null.
     *  @param {string} id @param {string} value @param {Rect} rect
     *  @param {FieldOptions} [options] @returns {string | null} */
    function inlineEdit(id, value, rect, options = {}) {
        const key = ctx.fullId(id);
        const fieldId = id + ':field';
        const theme = t();
        const px = ctx.px;
        if (inlineEditId !== key) {
            const state = ctx.interact(id + ':label', rect, {
                cursor: 'text', a11y: { role: 'button', label: value + (options.label ? ', ' + options.label : '') },
            });
            const size = Math.min(px(32), rect.height * 0.7);
            const visible = ctx.truncate(value, rect.width, size);
            ui.label(visible, rect, { size });
            const underline = ctx.springAt(state.id + '#underline', state.hovered ? 1 : 0, springs.snappy);
            if (underline > 0.01)
                ctx.line(rect.x, rect.y + rect.height - 2,
                    rect.x + ctx.measure(visible, size) * underline, rect.y + rect.height - 2, theme.accent);
            if (state.clicked) {
                inlineEditId = key;
                inlineDraft.value = value;
                focusField(fieldId, inlineDraft);
            } else return null;
        }
        const buttonWidth = Math.min(px(64), Math.max(px(48), rect.width * 0.24));
        const [editorRect, okRect] = ui.columns(rect, [{ weight: 1 }, buttonWidth], px(8));
        field(fieldId, editorRect, inlineDraft, options);
        const valid = inlineDraft.value.trim().length > 0;
        const confirmed = ui.button(id + ':ok', 'OK', okRect, { primary: true, enabled: valid });
        if (sys.input.isKeyPressed(KEY_ESCAPE) || (outsideGesture() && !ctx.inside(rect))) {
            inlineEditId = null;
            ctx.focus = null;
            sys.input.stopTextInput();
        } else if (valid && (confirmed || sys.input.isKeyPressed(KEY_ENTER))) {
            inlineEditId = null;
            ctx.focus = null;
            sys.input.stopTextInput();
            return inlineDraft.value.trim();
        }
        return null;
    }

    /** The pointer gesture that leaves a field: a mouse press, or a touch tap. */
    function outsideGesture() {
        return ctx.input.pointer.type === 'touch' ? ctx.tapped : ctx.input.pointer.pressed;
    }

    /** End-of-frame focus rules (called by ui.end). */
    ui._endText = () => {
        if (inlineEditId !== null && !ctx.seen.has(inlineEditId + ':field')) inlineEditId = null;
        if (ctx.focus !== null && !ctx.seen.has(ctx.focus)) {
            ctx.focus = null;
            sys.input.stopTextInput();
        }
        // Pressing (mouse) or tapping (touch) elsewhere ends editing; on touch
        // a scroll gesture keeps the field and its keyboard.
        const pointerOn = ctx.input.pointer.type === 'touch' ? ctx.releasedOn : ctx.active;
        if (outsideGesture() && ctx.focus !== null && pointerOn !== ctx.focus) {
            ctx.focus = null;
            sys.input.stopTextInput();
        }
        // Keyboard focus moved to another widget.
        if (ctx.focus !== null && ctx.keyFocus !== null && ctx.keyFocus !== ctx.focus) {
            ctx.focus = null;
            sys.input.stopTextInput();
        }
    };

    Object.assign(ui, { field, focusField, inlineEdit });
}
