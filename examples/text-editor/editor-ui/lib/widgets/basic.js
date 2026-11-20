/** Buttons, labels, panels, choices, toggles, checkboxes, sliders. */

import { clamp } from '../math.js';
import { withAlpha } from '../color.js';
import { springs } from '../motion.js';
import { scaleRect } from '../layout.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {{ primary?: boolean, variant?: 'secondary' | 'primary' | 'ghost' | 'danger', enabled?: boolean }} ButtonOptions */
/** @typedef {{ size?: number, color?: import('../color.js').Color }} LabelOptions */
/** @typedef {{ padding?: number, color?: import('../color.js').Color }} PanelOptions */
/** @typedef {{ min?: number, max?: number, label?: string }} SliderOptions */

// USB HID scancodes
const KEY_RIGHT = 79, KEY_LEFT = 80, KEY_HOME = 74, KEY_END = 77;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;

    /** @param {string} id @param {string} label @param {Rect} rect
     *  @param {ButtonOptions} [options] @returns {boolean} true when clicked */
    function button(id, label, rect, options = {}) {
        const enabled = options.enabled !== false;
        const a11y = { role: 'button', label, disabled: !enabled };
        let state;
        if (enabled) state = ctx.interact(id, rect, { a11y });
        else {
            state = { hovered: false, held: false, clicked: false, id: ctx.claim(id) };
            ctx.describe(state.id, rect, a11y);
        }
        const theme = t();
        // Variants: secondary (default, outlined), primary (filled accent),
        // ghost (no fill until hovered), danger (filled danger color).
        const variant = options.variant || (options.primary ? 'primary' : 'secondary');
        const filled = variant === 'primary' || variant === 'danger';
        const fillColor = variant === 'danger' ? theme.danger : theme.accent;
        const clear = withAlpha(theme.raised, 0);
        const background = !enabled ? (variant === 'ghost' ? clear : theme.raised)
            : filled ? (state.held ? theme.ink : fillColor)
                : state.held ? theme.border : state.hovered ? theme.raised : variant === 'ghost' ? clear : theme.surface;
        const ink = !enabled ? theme.muted : filled ? theme.accentInk : variant === 'ghost' ? theme.accent : theme.ink;
        const press = ctx.springAt(state.id + '#press', state.held ? 1 : 0, springs.snappy);
        const shape = scaleRect(rect, 1 - 0.035 * press);
        const backgroundColor = ctx.colorAt(state.id + '#bg', background);
        if (variant === 'primary' && enabled && !state.held) ctx.fillAccent(state.id + '#bg', shape, backgroundColor);
        else ctx.fill(shape, backgroundColor);
        // Secondary buttons get an outline so they stand out from surfaces.
        if (enabled && variant === 'secondary') ctx.outline(shape, ctx.colorAt(state.id + '#line', state.hovered ? theme.muted : theme.border), 1);
        const size = ctx.sp(theme.font);
        const width = ctx.measure(label, size);
        const inkColor = ctx.colorAt(state.id + '#ink', ink);
        // An optional icon sits before the label; the pair stays centered.
        const iconSize = options.icon ? Math.round(size * 1.25) : 0;
        const iconGap = options.icon && label ? Math.round(size * 0.45) : 0;
        const left = shape.x + (shape.width - width - iconSize - iconGap) / 2;
        const clip = ctx.pushClip(rect);
        if (options.icon)
            ctx.icon(options.icon, { x: left, y: shape.y + (shape.height - iconSize) / 2, width: iconSize, height: iconSize },
                { color: inkColor });
        ctx.text(label, left + iconSize + iconGap, shape.y + shape.height / 2 + size * 0.35, size, inkColor);
        ctx.popClip(clip);
        return state.clicked;
    }

    /** A button showing only an icon: ghost and round by default. Pass
     *  `label`: screen readers announce it (the icon name otherwise).
     *  @param {string} id @param {string} icon @param {Rect} rect
     *  @param {{ label?: string, variant?: 'secondary' | 'primary' | 'ghost' | 'danger', enabled?: boolean, size?: number }} [options]
     *  @returns {boolean} */
    function iconButton(id, icon, rect, options = {}) {
        const enabled = options.enabled !== false;
        const a11y = { role: 'button', label: options.label || icon, disabled: !enabled };
        let state;
        if (enabled) state = ctx.interact(id, rect, { a11y });
        else {
            state = { hovered: false, held: false, clicked: false, id: ctx.claim(id) };
            ctx.describe(state.id, rect, a11y);
        }
        const theme = t();
        const variant = options.variant || 'ghost';
        const filled = variant === 'primary' || variant === 'danger';
        const fillColor = variant === 'danger' ? theme.danger : theme.accent;
        const clear = withAlpha(theme.raised, 0);
        const background = !enabled ? clear
            : filled ? (state.held ? theme.ink : fillColor)
                : state.held ? theme.border : state.hovered ? theme.raised : variant === 'ghost' ? clear : theme.surface;
        const ink = !enabled ? theme.muted : filled ? theme.accentInk : theme.ink;
        const press = ctx.springAt(state.id + '#press', state.held ? 1 : 0, springs.snappy);
        const shape = scaleRect(rect, 1 - 0.06 * press);
        const radius = Math.min(shape.width, shape.height) / 2;
        ctx.fill(shape, ctx.colorAt(state.id + '#bg', background), radius);
        if (enabled && variant === 'secondary') ctx.outline(shape, ctx.colorAt(state.id + '#line', state.hovered ? theme.muted : theme.border), 1, radius);
        const size = options.size || Math.min(Math.round(Math.min(rect.width, rect.height) * 0.55), ctx.px(24));
        ctx.icon(icon, { x: shape.x + (shape.width - size) / 2, y: shape.y + (shape.height - size) / 2, width: size, height: size },
            { color: ctx.colorAt(state.id + '#ink', ink) });
        return state.clicked;
    }

    /** @param {string | number} value @param {Rect} rect @param {LabelOptions} [options] */
    function label(value, rect, options = {}) {
        const size = options.size || ctx.sp(t().font);
        ctx.describeText(rect, String(value));
        ctx.text(value, rect.x, rect.y + Math.min(rect.height, size * 1.25), size, options.color || t().ink);
    }

    /** @param {Rect} rect @param {(inner: Rect) => void} draw @param {PanelOptions} [options] */
    function panel(rect, draw, options = {}) {
        ctx.fill(rect, options.color || t().surface);
        draw(ui.inset(rect, options.padding === undefined ? ctx.px(t().padding) : options.padding));
    }

    /** A selectable row (e.g. in a menu). @param {string} id @param {string} value
     *  @param {Rect} rect @param {boolean} [selected] @returns {boolean} true when clicked */
    function choice(id, value, rect, selected = false) {
        const state = ctx.interact(id, rect, { a11y: { role: 'option', label: value, selected } });
        const theme = t();
        const target = selected ? theme.raised : state.hovered ? theme.background : withAlpha(theme.background, 0);
        ctx.fill(rect, ctx.colorAt(state.id + '#bg', target));
        const size = ctx.sp(theme.font), inner = ui.inset(rect, ctx.px(10));
        ctx.text(value, inner.x, inner.y + Math.min(inner.height, size * 1.25), size,
            ctx.colorAt(state.id + '#ink', selected ? theme.accent : theme.ink));
        return state.clicked;
    }

    /** @param {string} id @param {string} labelText @param {Rect} rect
     *  @param {boolean} value @returns {boolean} the new value */
    function toggle(id, labelText, rect, value) {
        const state = ctx.interact(id, rect, { a11y: { role: 'switch', label: labelText, checked: value } });
        const next = state.clicked ? !value : value;
        if (next !== value) ui.haptic('selection');
        const theme = t();
        const px = ctx.px;
        const size = ctx.sp(theme.font);
        ctx.text(labelText, rect.x, rect.y + rect.height / 2 + size * 0.35, size, theme.ink);
        const track = {
            x: rect.x + rect.width - px(46), y: rect.y + (rect.height - px(24)) / 2,
            width: px(44), height: px(24)
        };
        const on = ctx.springAt(state.id + '#on', next ? 1 : 0, springs.snappy);
        const trackColor = ctx.colorAt(state.id + '#track', next ? theme.accent : theme.border);
        if (theme.accentGradient && on > 0.001) {
            // The gradient fades in over the off color as the switch turns on.
            ctx.fill(track, theme.border, px(12));
            const alpha = ctx.alpha;
            ctx.alpha = alpha * Math.min(1, on);
            ctx.fillAccent(state.id + '#track', track, trackColor, px(12));
            ctx.alpha = alpha;
        } else ctx.fill(track, trackColor, px(12));
        const knob = px(18) * (1 + 0.12 * ctx.springAt(state.id + '#press', state.held ? 1 : 0, springs.snappy));
        ctx.fill({
            x: track.x + px(3) + (track.width - px(6) - px(18)) * on - (knob - px(18)) / 2,
            y: track.y + (track.height - knob) / 2, width: knob, height: knob
        }, theme.surface, knob / 2);
        return next;
    }

    /** @param {string} id @param {string} labelText @param {Rect} rect
     *  @param {boolean} value @returns {boolean} the new value */
    function checkbox(id, labelText, rect, value) {
        const state = ctx.interact(id, rect, { a11y: { role: 'checkbox', label: labelText, checked: value } });
        const next = state.clicked ? !value : value;
        if (next !== value) ui.haptic('selection');
        const theme = t();
        const px = ctx.px;
        const box = { x: rect.x + px(2), y: rect.y + (rect.height - px(24)) / 2, width: px(24), height: px(24) };
        const check = ctx.springAt(state.id + '#check', next ? 1 : 0, springs.snappy);
        ctx.fill(box, ctx.colorAt(state.id + '#box', next ? theme.accent : theme.surface), px(3));
        if (check < 0.999) ctx.outline(box, theme.border, 1, px(3));
        if (check > 0.001) {
            // The check draws itself in: first stroke, then the second.
            const first = clamp(check / 0.4, 0, 1), second = clamp((check - 0.4) / 0.6, 0, 1);
            const ax = box.x + px(5), ay = box.y + px(12), bx = box.x + px(10), by = box.y + px(18);
            const cx = box.x + px(20), cy = box.y + px(6);
            ctx.line(ax, ay, ax + (bx - ax) * first, ay + (by - ay) * first, theme.accentInk, px(3));
            if (second > 0) ctx.line(bx, by, bx + (cx - bx) * second, by + (cy - by) * second, theme.accentInk, px(3));
        }
        const size = ctx.sp(theme.font);
        ctx.text(labelText, rect.x + px(38), rect.y + rect.height / 2 + size * 0.35, size, theme.ink);
        return next;
    }

    /** @param {string} id @param {Rect} rect @param {number} value
     *  @param {SliderOptions} [options] @returns {number} the new value */
    function slider(id, rect, value, options = {}) {
        const min = options.min === undefined ? 0 : options.min;
        const max = options.max === undefined ? 1 : options.max;
        const percent = Math.round(100 * (value - min) / Math.max(0.00001, max - min));
        const state = ctx.interact(id, rect, {
            cursor: 'grab',
            a11y: { role: 'slider', label: options.label || '', value: percent + '%', min, max, rangeValue: value },
        });
        // Screen readers step by a tenth of the range.
        const step = state.action?.action === 'increment' ? 1 : state.action?.action === 'decrement' ? -1 : 0;
        if (step) value = clamp(value + step * (max - min) / 10, min, max);
        // Keyboard: arrows step by a twentieth, Home and End jump to the ends.
        if (state.focused) {
            if (ctx.keyPressed(KEY_RIGHT)) value = clamp(value + (max - min) / 20, min, max);
            if (ctx.keyPressed(KEY_LEFT)) value = clamp(value - (max - min) / 20, min, max);
            if (ctx.keyPressed(KEY_HOME)) value = min;
            if (ctx.keyPressed(KEY_END)) value = max;
        }
        const theme = t();
        const px = ctx.px;
        const x = rect.x + px(10);
        const width = Math.max(1, rect.width - px(20));
        if (state.held) value = clamp(min + (max - min) * (ctx.input.pointer.x - x) / width, min, max);
        const fraction = clamp((value - min) / Math.max(0.00001, max - min), 0, 1);
        const y = rect.y + rect.height / 2;
        ctx.fill({ x, y: y - px(3), width, height: px(6) }, theme.border, px(3));
        ctx.fill({ x, y: y - px(3), width: width * fraction, height: px(6) },
            ctx.colorAt(state.id + '#fill', theme.accent), px(3));
        const grow = ctx.springAt(state.id + '#grow', state.held ? 1 : state.hovered ? 0.5 : 0, springs.snappy);
        const radius = px(9) * (1 + 0.3 * grow);
        if (grow > 0.01)
            ctx.fill({ x: x + width * fraction - radius * 1.8, y: y - radius * 1.8, width: radius * 3.6, height: radius * 3.6 },
                withAlpha(theme.accent, 0.14 * grow), radius * 1.8);
        ctx.fill({ x: x + width * fraction - radius, y: y - radius, width: radius * 2, height: radius * 2 },
            theme.accent, radius);
        return value;
    }

    Object.assign(ui, { button, iconButton, label, panel, choice, toggle, checkbox, slider });
}
