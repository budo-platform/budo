/// <reference path="../../budo.d.ts" />

/** @typedef {{ x: number, y: number, width: number, height: number }} Rect */
/** @typedef {{ weight?: number }} WeightedSize */
/** @typedef {number | WeightedSize} LayoutSize */
/** @typedef {{ hovered: boolean, held: boolean, clicked: boolean }} Interaction */
/** @typedef {{ value: string }} TextModel */
/** @typedef {{ start: number, end: number, composition: string,
 *   compositionCaret: number, scrollX: number, blinkStarted: number }} EditorState */
/** @typedef {{ pointerId: number, lastY: number, startY: number, dragging: boolean }} ScrollGesture */
/** @typedef {(rect: Rect) => void} RectPainter */
/** @typedef {(content: Rect, offset: number) => void} ScrollPainter */
/** @typedef {{ primary?: boolean, enabled?: boolean }} ButtonOptions */
/** @typedef {{ size?: number, color?: string }} LabelOptions */
/** @typedef {{ padding?: number, color?: string }} PanelOptions */
/** @typedef {{ ratio?: number, min?: number, max?: number, vertical?: boolean }} SplitOptions */
/** @typedef {{ min?: number, max?: number }} SliderOptions */
/** @typedef {{ placeholder?: string, maxLength?: number }} FieldOptions */
/** @typedef {{ id: string, title: string, subtitle?: string, depth?: number,
 *   kind?: 'group' | 'item', groupId?: string, height?: number,
 *   action?: boolean, actionLabel?: string }} ListItem */
/** @typedef {{ selectedId?: string | null, rowHeight?: number, indent?: number,
 *   emptyText?: string, reorder?: boolean }} ListOptions */
/** @typedef {{ selectedId: string | null, actionId: string | null,
 *   move: { fromId: string, toId: string } | null }} ListResult */
/** @typedef {{ sourceId: string, startY: number, moved: boolean }} ListDrag */
/** @typedef {{ background: string, surface: string, raised: string, ink: string, muted: string,
 *   border: string, accent: string, accentInk: string, highlight: string,
 *   font: number, row: number, radius: number }} UITheme */

/** @type {UITheme} */
const defaultTheme = {
    background: '#F3F5F2', surface: '#FFFFFF', raised: '#E8EEEA',
    ink: '#20312F', muted: '#61716D', border: '#C9D5CF',
    accent: '#006F67', accentInk: '#FFFFFF', highlight: '#F3AC59',
    font: 20, row: 48, radius: 5,
};

/** @type {(value: number, low: number, high: number) => number} */
export const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
/** @type {(from: number, to: number, fraction: number) => number} */
export const lerp = (from, to, fraction) => from + (to - from) * fraction;
/** @type {(halfLife: number, dt: number) => number} */
export const smoothing = (halfLife, dt) => 1 - Math.pow(0.5, dt / Math.max(0.0001, halfLife));

export class Smoothed {
    /** @param {number} [value] @param {number} [halfLife] */
    constructor(value = 0, halfLife = 0.15) {
        this.value = value;
        this.target = value;
        this.halfLife = halfLife;
    }
    /** @param {number} dt @returns {number} */
    update(dt) {
        this.value = lerp(this.value, this.target, smoothing(this.halfLife, dt));
        return this.value;
    }
}

/** @param {Partial<UITheme>} [theme] */
export function createUI(theme = {}) {
    const colors = { ...defaultTheme, ...theme };
    /** @type {InputState | null} */
    let input = null;
    /** @type {Rect | null} */
    let bounds = null;
    /** @type {Rect | null} */
    let clip = null;
    /** @type {string | null} */
    let active = null;
    /** @type {string | null} */
    let focus = null;
    /** @type {string | null} */
    let freshFocus = null;
    let pressedOnWidget = false;
    /** @type {Set<string>} */
    let seen = new Set();
    /** @type {Map<string, number>} */
    const offsets = new Map();
    /** @type {Map<string, number>} */
    const scrollLimits = new Map();
    /** @type {Map<string, ScrollGesture>} */
    const scrollGestures = new Map();
    /** @type {Map<string, number>} */
    const ratios = new Map();
    /** @type {Map<string, EditorState>} */
    const editors = new Map();
    /** @type {Map<string, ListDrag>} */
    const listDrags = new Map();
    /** @type {string | null} */
    let inlineEditId = null;
    const inlineDraft = { value: '' };

    /** @param {Rect} rect @returns {boolean} */
    function inside(rect) {
        const { x, y } = input.pointer;
        return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
            x < rect.x + rect.width && y < rect.y + rect.height &&
            (!clip || (x >= clip.x && y >= clip.y && x < clip.x + clip.width && y < clip.y + clip.height));
    }

    /** @param {string} id @param {Rect} rect @returns {Interaction} */
    function interact(id, rect) {
        if (seen.has(id)) throw new Error('Duplicate UI id: ' + id);
        seen.add(id);
        const hovered = inside(rect);
        if (hovered && input.pointer.pressed) {
            active = id;
            pressedOnWidget = true;
        }
        const held = active === id && input.pointer.down;
        const clicked = active === id && hovered && !input.pointer.down && !input.pointer.pressed;
        if (active === id && !input.pointer.down) active = null;
        return { hovered, held, clicked };
    }

    /** @param {Rect} rect @param {Color} color @param {number} [radius] */
    function fill(rect, color, radius = colors.radius) {
        sys.canvas.setFillColor(color);
        sys.canvas.drawRoundRect(rect.x, rect.y, Math.max(0, rect.width), Math.max(0, rect.height), radius, radius);
    }

    /** @param {string | number} value @param {number} x @param {number} y
     *  @param {number} [size] @param {Color} [color] */
    function text(value, x, y, size = colors.font, color = colors.ink) {
        sys.canvas.setFillColor(color);
        sys.canvas.drawText(String(value), x, y, size);
    }

    /** @param {InputState} frameInput @param {Rect} frameBounds */
    function begin(frameInput, frameBounds) {
        input = frameInput;
        bounds = frameBounds;
        clip = null;
        seen = new Set();
        pressedOnWidget = false;
    }

    /** @returns {void} */
    function end() {
        if (inlineEditId !== null && !seen.has(inlineEditId + ':field')) inlineEditId = null;
        if (focus !== null && !seen.has(focus)) {
            focus = null;
            sys.input.stopTextInput();
        }
        if (input.pointer.pressed && focus !== null && active !== focus) {
            focus = null;
            sys.input.stopTextInput();
        }
        if (!input.pointer.down) active = null;
    }

    /** @param {string} id @param {string} label @param {Rect} rect
     *  @param {ButtonOptions} [options] @returns {boolean} */
    function button(id, label, rect, options = {}) {
        const enabled = options.enabled !== false;
        if (!enabled) {
            if (seen.has(id)) throw new Error('Duplicate UI id: ' + id);
            seen.add(id);
        }
        const state = enabled ? interact(id, rect) : { hovered: false, held: false, clicked: false };
        fill(rect, !enabled ? colors.raised : options.primary ? (state.held ? colors.ink : colors.accent) :
            state.hovered ? colors.raised : colors.surface);
        const ink = !enabled ? colors.muted : options.primary ? colors.accentInk : colors.ink;
        const width = sys.canvas.measureText(label, colors.font);
        sys.canvas.save();
        sys.canvas.clipRect(rect.x, rect.y, rect.width, rect.height);
        text(label, rect.x + (rect.width - width) / 2,
            rect.y + rect.height / 2 + colors.font * 0.35, colors.font, ink);
        sys.canvas.restore();
        return state.clicked;
    }

    /** @param {Rect} rect @param {number} padding @returns {Rect} */
    function inset(rect, padding) {
        return {
            x: rect.x + padding, y: rect.y + padding,
            width: Math.max(0, rect.width - padding * 2),
            height: Math.max(0, rect.height - padding * 2)
        };
    }

    /** @param {string | number} value @param {Rect} rect @param {LabelOptions} [options] */
    function label(value, rect, options = {}) {
        text(value, rect.x, rect.y + Math.min(rect.height, (options.size || colors.font) * 1.25),
            options.size || colors.font, options.color || colors.ink);
    }

    /** @param {Rect} rect @param {RectPainter} draw @param {PanelOptions} [options] */
    function panel(rect, draw, options = {}) {
        fill(rect, options.color || colors.surface);
        draw(inset(rect, options.padding === undefined ? 16 : options.padding));
    }

    /** @param {Rect} rect @param {LayoutSize[]} sizes @param {number} [gap]
     *  @param {boolean} [horizontal] @returns {Rect[]} */
    function stack(rect, sizes, gap = 8, horizontal = false) {
        const length = horizontal ? rect.width : rect.height;
        const remaining = Math.max(0, length - gap * Math.max(0, sizes.length - 1) -
            sizes.reduce((sum, size) => sum + (typeof size === 'number' ? size : 0), 0));
        const weight = sizes.reduce((sum, size) => sum + (typeof size === 'number' ? 0 : size.weight || 1), 0);
        let position = horizontal ? rect.x : rect.y;
        return sizes.map(size => {
            const extent = typeof size === 'number' ? size : remaining * (size.weight || 1) / weight;
            const child = horizontal
                ? { x: position, y: rect.y, width: Math.max(0, extent), height: rect.height }
                : { x: rect.x, y: position, width: rect.width, height: Math.max(0, extent) };
            position += extent + gap;
            return child;
        });
    }

    /** @param {Rect} rect @param {LayoutSize[]} sizes @param {number} [gap] @returns {Rect[]} */
    function rows(rect, sizes, gap = 8) { return stack(rect, sizes, gap); }
    /** @param {Rect} rect @param {LayoutSize[]} sizes @param {number} [gap] @returns {Rect[]} */
    function columns(rect, sizes, gap = 8) { return stack(rect, sizes, gap, true); }

    /** @param {string} id @param {Rect} rect @param {RectPainter} drawFirst
     *  @param {RectPainter} drawSecond @param {SplitOptions} [options] */
    function split(id, rect, drawFirst, drawSecond, options = {}) {
        const vertical = options.vertical === true;
        const length = vertical ? rect.height : rect.width;
        const ratio = ratios.has(id) ? ratios.get(id) : options.ratio || 0.5;
        const bar = length * ratio;
        const divider = vertical
            ? { x: rect.x, y: rect.y + bar - 4, width: rect.width, height: 8 }
            : { x: rect.x + bar - 4, y: rect.y, width: 8, height: rect.height };
        const first = vertical
            ? { x: rect.x, y: rect.y, width: rect.width, height: Math.max(0, bar - 5) }
            : { x: rect.x, y: rect.y, width: Math.max(0, bar - 5), height: rect.height };
        const second = vertical
            ? { x: rect.x, y: rect.y + bar + 5, width: rect.width, height: Math.max(0, length - bar - 5) }
            : { x: rect.x + bar + 5, y: rect.y, width: Math.max(0, length - bar - 5), height: rect.height };
        drawFirst(first);
        drawSecond(second);
        const hit = vertical
            ? { ...divider, y: divider.y - 14, height: 36 }
            : { ...divider, x: divider.x - 14, width: 36 };
        const state = interact(id, hit);
        fill(divider, state.hovered || state.held ? colors.accent : colors.border, 2);
        if (state.held) ratios.set(id, clamp(((vertical ? input.pointer.y - rect.y : input.pointer.x - rect.x) /
            Math.max(1, length)), options.min || 0.2, options.max || 0.8));
    }

    /** @param {Rect} first @param {Rect} second @returns {Rect} */
    function intersect(first, second) {
        const x = Math.max(first.x, second.x);
        const y = Math.max(first.y, second.y);
        return {
            x, y, width: Math.max(0, Math.min(first.x + first.width, second.x + second.width) - x),
            height: Math.max(0, Math.min(first.y + first.height, second.y + second.height) - y)
        };
    }

    /** @param {string} id @param {Rect} rect @param {number} contentHeight
     *  @param {ScrollPainter} draw @param {{ touchScroll?: boolean, followEnd?: boolean }} [options] @returns {number} */
    function scroll(id, rect, contentHeight, draw, options = {}) {
        if (seen.has(id)) throw new Error('Duplicate UI id: ' + id);
        seen.add(id);
        const limit = Math.max(0, contentHeight - rect.height);
        const previousOffset = offsets.get(id) || 0;
        const previousLimit = scrollLimits.get(id) || 0;
        let offset = options.followEnd && previousOffset >= previousLimit - 2
            ? limit : clamp(previousOffset, 0, limit);
        if (inside(rect)) offset = clamp(offset - (input.mouse?.wheelY || 0) * 38, 0, limit);
        if (input.pointer.type === 'touch' && options.touchScroll !== false) {
            if (input.pointer.pressed && inside(rect))
                scrollGestures.set(id, {
                    pointerId: input.pointer.id, lastY: input.pointer.y,
                    startY: input.pointer.y, dragging: false
                });
            const gesture = scrollGestures.get(id);
            if (gesture && gesture.pointerId === input.pointer.id) {
                if (input.pointer.down) {
                    if (Math.abs(input.pointer.y - gesture.startY) > 7) gesture.dragging = true;
                    if (gesture.dragging) {
                        offset = clamp(offset + gesture.lastY - input.pointer.y, 0, limit);
                        active = null;
                    }
                    gesture.lastY = input.pointer.y;
                } else {
                    if (gesture.dragging) active = null;
                    scrollGestures.delete(id);
                }
            }
        } else scrollGestures.delete(id);
        offsets.set(id, offset);
        scrollLimits.set(id, limit);
        const previousClip = clip;
        clip = previousClip ? intersect(rect, previousClip) : rect;
        sys.canvas.save();
        sys.canvas.clipRect(clip.x, clip.y, clip.width, clip.height);
        try {
            draw({ x: rect.x, y: rect.y - offset, width: rect.width, height: contentHeight }, offset);
        } finally {
            sys.canvas.restore();
            clip = previousClip;
        }
        if (limit > 0) {
            const thumb = Math.max(24, rect.height * rect.height / contentHeight);
            fill({
                x: rect.x + rect.width - 5, y: rect.y + (rect.height - thumb) * offset / limit,
                width: 4, height: thumb
            }, colors.border, 2);
        }
        return offset;
    }

    /** @param {string} id @param {string} value @param {Rect} rect
     *  @param {boolean} [selected] @returns {boolean} */
    function choice(id, value, rect, selected = false) {
        const state = interact(id, rect);
        if (selected || state.hovered) fill(rect, selected ? colors.raised : colors.background);
        label(value, inset(rect, 10), { color: selected ? colors.accent : colors.ink });
        return state.clicked;
    }

    /** @param {string} value @param {number} width @param {number} size @returns {string} */
    function truncate(value, width, size) {
        if (width <= 0) return '';
        if (sys.canvas.measureText(value, size) <= width) return value;
        let end = value.length;
        while (end > 0 && sys.canvas.measureText(value.slice(0, end) + '...', size) > width) end--;
        return end ? value.slice(0, end) + '...' : '';
    }

    /** Resolve a horizontal text position to the nearest UTF-16 string index.
     *  @param {string} value @param {number} x @param {number} size @returns {number} */
    function textIndexAt(value, x, size) {
        if (x <= 0) return 0;
        let index = 0;
        let previousWidth = 0;
        for (const character of value) {
            const nextIndex = index + character.length;
            const nextWidth = sys.canvas.measureText(value.slice(0, nextIndex), size);
            if (x < (previousWidth + nextWidth) / 2) return index;
            index = nextIndex;
            previousWidth = nextWidth;
        }
        return value.length;
    }

    /** Draw a clipped, variable-height list. The app owns its items and selection.
     *  @param {string} id @param {Rect} rect @param {ListItem[]} items
     *  @param {ListOptions} [options] @returns {ListResult} */
    function list(id, rect, items, options = {}) {
        const rowHeight = options.rowHeight || 52;
        const indent = options.indent === undefined ? 20 : options.indent;
        let contentHeight = 0;
        const positions = items.map(item => {
            const top = contentHeight;
            contentHeight += item.height || rowHeight;
            return top;
        });
        /** @type {ListResult} */
        const result = { selectedId: null, actionId: null, move: null };
        const scrollId = id + ':scroll';
        const drag = listDrags.get(id);
        if (drag && input.pointer.down && drag.moved) {
            const edge = Math.min(32, rect.height / 4);
            const direction = input.pointer.y < rect.y + edge ? -1 :
                input.pointer.y > rect.y + rect.height - edge ? 1 : 0;
            if (direction) offsets.set(scrollId, clamp((offsets.get(scrollId) || 0) + direction * 8,
                0, Math.max(0, contentHeight - rect.height)));
        }
        scroll(scrollId, rect, contentHeight, (content) => {
            if (!items.length) label(options.emptyText || 'No items', inset(rect, 12), { color: colors.muted });
            items.forEach((item, index) => {
                const height = item.height || rowHeight;
                const y = content.y + positions[index];
                if (y + height <= rect.y || y >= rect.y + rect.height) return;
                const depth = Math.max(0, item.depth || 0);
                const row = {
                    x: content.x + depth * indent, y: y + 2,
                    width: Math.max(0, content.width - depth * indent - 8), height: Math.max(0, height - 4)
                };
                const actionWidth = item.action ? 38 : 0;
                const handleWidth = options.reorder ? 32 : 0;
                const body = { ...row, width: Math.max(0, row.width - actionWidth - handleWidth) };
                const state = interact(id + ':row:' + item.id, body);
                const selected = item.id === options.selectedId;
                if (selected || state.hovered) fill(row, selected ? colors.raised : colors.background);
                const size = item.kind === 'group' ? colors.font : Math.max(15, colors.font - 2);
                const titleWidth = Math.max(0, body.width - 24);
                text(truncate(item.title, titleWidth, size), body.x + 12,
                    row.y + (item.subtitle ? row.height * 0.48 : row.height / 2 + size * 0.35),
                    size, selected ? colors.accent : colors.ink);
                if (item.subtitle) text(truncate(item.subtitle, titleWidth, 15), body.x + 12,
                    row.y + row.height - 9, 15, colors.muted);
                if (state.clicked) result.selectedId = item.id;
                if (item.action) {
                    const action = { x: body.x + body.width, y: row.y, width: actionWidth, height: row.height };
                    if (interact(id + ':action:' + item.id, action).clicked) result.actionId = item.id;
                    const actionLabel = item.actionLabel || '...';
                    text(actionLabel, action.x + (actionWidth - sys.canvas.measureText(actionLabel, colors.font)) / 2,
                        action.y + action.height / 2 + 5, colors.font, colors.muted);
                }
                if (options.reorder) {
                    const handle = {
                        x: row.x + row.width - handleWidth, y: row.y,
                        width: handleWidth, height: row.height
                    };
                    const handleId = id + ':drag:' + item.id;
                    const handleState = interact(handleId, handle);
                    if (input.pointer.pressed && handleState.hovered) {
                        listDrags.set(id, { sourceId: item.id, startY: input.pointer.y, moved: false });
                        scrollGestures.delete(scrollId);
                    }
                    text('::', handle.x + 7, handle.y + handle.height / 2 + 5, colors.font, colors.muted);
                }
            });
        }, { touchScroll: !drag });
        const currentDrag = listDrags.get(id);
        if (currentDrag) {
            if (input.pointer.down) {
                if (Math.abs(input.pointer.y - currentDrag.startY) > 7) currentDrag.moved = true;
            } else {
                const source = items.find(item => item.id === currentDrag.sourceId);
                if (currentDrag.moved && source && input.pointer.y >= rect.y &&
                    input.pointer.y < rect.y + rect.height) {
                    const contentY = input.pointer.y - rect.y + (offsets.get(scrollId) || 0);
                    const targetIndex = positions.findIndex((top, index) =>
                        contentY >= top && contentY < top + (items[index].height || rowHeight));
                    const target = items[targetIndex];
                    if (target && target.id !== source.id && target.kind === source.kind &&
                        target.groupId === source.groupId)
                        result.move = { fromId: source.id, toId: target.id };
                }
                listDrags.delete(id);
            }
        }
        return result;
    }

    /** @param {string} id @param {string} labelText @param {Rect} rect
     *  @param {boolean} value @returns {boolean} */
    function toggle(id, labelText, rect, value) {
        const state = interact(id, rect);
        label(labelText, { ...rect, width: Math.max(0, rect.width - 55) });
        const track = {
            x: rect.x + rect.width - 46, y: rect.y + (rect.height - 24) / 2,
            width: 44, height: 24
        };
        fill(track, value ? colors.accent : colors.border, 12);
        fill({ x: track.x + (value ? 23 : 3), y: track.y + 3, width: 18, height: 18 }, colors.surface, 9);
        return state.clicked ? !value : value;
    }

    /** @param {string} id @param {string} labelText @param {Rect} rect
     *  @param {boolean} value @returns {boolean} */
    function checkbox(id, labelText, rect, value) {
        const state = interact(id, rect);
        const box = { x: rect.x + 2, y: rect.y + (rect.height - 24) / 2, width: 24, height: 24 };
        fill(box, value ? colors.accent : colors.surface, 3);
        sys.canvas.setStrokeColor(value ? colors.accentInk : colors.border);
        sys.canvas.setStrokeWidth(value ? 3 : 1);
        if (value) {
            sys.canvas.drawLine(box.x + 5, box.y + 12, box.x + 10, box.y + 18);
            sys.canvas.drawLine(box.x + 10, box.y + 18, box.x + 20, box.y + 6);
        } else sys.canvas.drawRoundRect(box.x, box.y, box.width, box.height, 3, 3);
        label(labelText, { x: rect.x + 38, y: rect.y, width: Math.max(0, rect.width - 38), height: rect.height });
        return state.clicked ? !value : value;
    }

    /** @param {string} id @param {Rect} rect @param {number} value
     *  @param {SliderOptions} [options] @returns {number} */
    function slider(id, rect, value, options = {}) {
        const state = interact(id, rect);
        const min = options.min === undefined ? 0 : options.min;
        const max = options.max === undefined ? 1 : options.max;
        const x = rect.x + 10;
        const width = Math.max(1, rect.width - 20);
        fill({ x, y: rect.y + rect.height / 2 - 3, width, height: 6 }, colors.border, 3);
        if (state.held) value = clamp(min + (max - min) * (input.pointer.x - x) / width, min, max);
        fill({
            x: x + width * clamp((value - min) / Math.max(0.00001, max - min), 0, 1) - 9,
            y: rect.y + rect.height / 2 - 9, width: 18, height: 18
        }, colors.accent, 9);
        return value;
    }

    /** @param {string} id @param {TextModel} model */
    function focusField(id, model) {
        if (focus !== null) sys.input.stopTextInput();
        focus = id;
        freshFocus = id;
        const editor = editors.get(id) || {
            start: 0, end: 0, composition: '', compositionCaret: 0,
            scrollX: 0, blinkStarted: 0
        };
        editor.start = editor.end = model.value.length;
        editor.composition = '';
        editor.compositionCaret = 0;
        editor.blinkStarted = input.totalTime || 0;
        editors.set(id, editor);
        sys.input.startTextInput({
            text: model.value, selectionStart: editor.end,
            selectionEnd: editor.end, multiline: false
        });
    }

    /** @param {string} id @param {Rect} rect @param {TextModel} model
     *  @param {FieldOptions} [options] @returns {string} */
    function field(id, rect, model, options = {}) {
        const state = interact(id, rect);
        const editor = editors.get(id) || {
            start: model.value.length, end: model.value.length,
            composition: '', compositionCaret: 0, scrollX: 0, blinkStarted: input.totalTime || 0
        };
        editors.set(id, editor);
        const inner = inset(rect, 12);
        const placedCaret = input.pointer.pressed && state.hovered;
        const justFocused = freshFocus === id;
        if (justFocused) freshFocus = null;
        const previousStart = editor.start;
        const previousEnd = editor.end;
        const previousText = model.value;
        const previousCompositionCaret = editor.compositionCaret;
        if (placedCaret) {
            const caret = textIndexAt(model.value,
                input.pointer.x - inner.x + editor.scrollX, colors.font);
            editor.start = editor.end = caret;
            editor.composition = '';
            editor.compositionCaret = 0;
            if (focus !== id) {
                if (focus !== null) sys.input.stopTextInput();
                focus = id;
                sys.input.startTextInput({
                    text: model.value, selectionStart: caret,
                    selectionEnd: caret, multiline: false
                });
            }
        }
        if (focus === id) {
            if (!placedCaret && !justFocused && input.textEdit) {
                model.value = input.textEdit.text;
                editor.start = input.textEdit.selectionStart;
                editor.end = input.textEdit.selectionEnd;
            } else if (!placedCaret && !justFocused && input.text) {
                model.value = model.value.slice(0, editor.start) + input.text + model.value.slice(editor.end);
                editor.start = editor.end = editor.start + input.text.length;
            } else if (!placedCaret && !justFocused && sys.input.isKeyPressed(42)) {
                const start = editor.start === editor.end ? Math.max(0, editor.start - 1) : editor.start;
                model.value = model.value.slice(0, start) + model.value.slice(editor.end);
                editor.start = editor.end = start;
            } else if (!placedCaret && !justFocused && (sys.input.isKeyPressed(80) || sys.input.isKeyPressed(79))) {
                const left = sys.input.isKeyPressed(80);
                let caret = left ? Math.min(editor.start, editor.end) : Math.max(editor.start, editor.end);
                if (editor.start === editor.end) {
                    if (left && caret > 0)
                        caret -= /[\uDC00-\uDFFF]/.test(model.value[caret - 1]) ? 2 : 1;
                    else if (!left && caret < model.value.length)
                        caret += /[\uD800-\uDBFF]/.test(model.value[caret]) ? 2 : 1;
                }
                editor.start = editor.end = caret;
            }
            if (options.maxLength && model.value.length > options.maxLength)
                model.value = model.value.slice(0, options.maxLength);
            editor.start = clamp(editor.start, 0, model.value.length);
            editor.end = clamp(editor.end, 0, model.value.length);
            if (input.composition?.changed) {
                editor.composition = input.composition.active ? input.composition.text : '';
                editor.compositionCaret = input.composition.selectionEnd;
            }
            if (placedCaret || editor.start !== previousStart || editor.end !== previousEnd ||
                model.value !== previousText || editor.compositionCaret !== previousCompositionCaret)
                editor.blinkStarted = input.totalTime || 0;
        }
        fill(rect, colors.surface);
        sys.canvas.setStrokeColor(focus === id ? colors.accent : colors.border);
        sys.canvas.setStrokeWidth(focus === id ? 2 : 1);
        sys.canvas.drawRoundRect(rect.x, rect.y, rect.width, rect.height, colors.radius, colors.radius);
        const prefix = model.value.slice(0, editor.start);
        const shown = editor.composition
            ? prefix + editor.composition + model.value.slice(editor.end)
            : model.value;
        const caretText = editor.composition
            ? prefix + editor.composition.slice(0, editor.compositionCaret)
            : model.value.slice(0, editor.end);
        const caretOffset = sys.canvas.measureText(caretText, colors.font);
        const shownWidth = sys.canvas.measureText(shown, colors.font);
        if (focus === id) {
            if (caretOffset < editor.scrollX) editor.scrollX = caretOffset;
            if (caretOffset > editor.scrollX + inner.width - 2)
                editor.scrollX = caretOffset - inner.width + 2;
        }
        editor.scrollX = clamp(editor.scrollX, 0, Math.max(0, shownWidth - inner.width + 2));
        const textX = inner.x - editor.scrollX;
        sys.canvas.save();
        sys.canvas.clipRect(inner.x, rect.y, inner.width, rect.height);
        text(shown || options.placeholder || '', textX, rect.y + rect.height / 2 + colors.font * 0.35,
            colors.font, shown ? colors.ink : colors.muted);
        const caretX = textX + caretOffset;
        if (focus === id) {
            if (editor.composition) {
                const underlineX = textX + sys.canvas.measureText(prefix, colors.font);
                sys.canvas.setStrokeColor(colors.highlight);
                sys.canvas.drawLine(underlineX, rect.y + rect.height - 8,
                    underlineX + sys.canvas.measureText(editor.composition, colors.font), rect.y + rect.height - 8);
            }
            if (Math.floor(((input.totalTime || 0) - editor.blinkStarted) * 2) % 2 === 0) {
                sys.canvas.setStrokeColor(colors.ink);
                sys.canvas.drawLine(caretX, rect.y + 10, caretX, rect.y + rect.height - 10);
            }
        }
        sys.canvas.restore();
        if (focus === id) sys.input.updateTextInput({
            text: model.value, selectionStart: editor.start,
            selectionEnd: editor.end, caret: { x: caretX, y: rect.y + 5, width: 1, height: rect.height - 10 }
        });
        return model.value;
    }

    /** @param {string} id @param {string} value @param {Rect} rect
     *  @param {FieldOptions} [options] @returns {string | null} */
    function inlineEdit(id, value, rect, options = {}) {
        const fieldId = id + ':field';
        if (inlineEditId !== id) {
            const state = interact(id + ':label', rect);
            const size = Math.min(32, rect.height * 0.7);
            const visible = truncate(value, rect.width, size);
            label(visible, rect, { size });
            if (state.hovered) {
                sys.canvas.setStrokeColor(colors.accent);
                sys.canvas.drawLine(rect.x, rect.y + rect.height - 2,
                    rect.x + sys.canvas.measureText(visible, size), rect.y + rect.height - 2);
            }
            if (state.clicked) {
                inlineEditId = id;
                inlineDraft.value = value;
                focusField(fieldId, inlineDraft);
            } else return null;
        }
        const buttonWidth = Math.min(64, Math.max(48, rect.width * 0.24));
        const [editorRect, okRect] = columns(rect, [{ weight: 1 }, buttonWidth], 8);
        field(fieldId, editorRect, inlineDraft, options);
        const valid = inlineDraft.value.trim().length > 0;
        const confirmed = button(id + ':ok', 'OK', okRect, { primary: true, enabled: valid });
        if (sys.input.isKeyPressed(41) || (input.pointer.pressed && !inside(rect))) {
            inlineEditId = null;
            focus = null;
            sys.input.stopTextInput();
        } else if (valid && (confirmed || sys.input.isKeyPressed(40))) {
            inlineEditId = null;
            focus = null;
            sys.input.stopTextInput();
            return inlineDraft.value.trim();
        }
        return null;
    }

    return {
        colors, begin, end, fill, text, button, label, panel, inset, rows, columns,
        split, scroll, list, choice, toggle, checkbox, slider, field, focusField, inlineEdit, interact,
        get input() { return input; }, get bounds() { return bounds; },
        get clip() { return clip; }, set clip(value) { clip = value; },
        get focus() { return focus; }, set focus(value) { focus = value; }
    };
}