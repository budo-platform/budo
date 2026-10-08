/** One-of-many choices: radio groups, segmented controls, and tabs. Each
 *  returns the (possibly new) value, like select. */

import { springs } from '../motion.js';
import { withAlpha } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {string | { value: string, label?: string, disabled?: boolean }} ChoiceOption */

/** @param {ChoiceOption[]} options */
const normalize = options => options.map(option => typeof option === 'string'
    ? { value: option, label: option, disabled: false }
    : { value: option.value, label: option.label || option.value, disabled: !!option.disabled });

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;

    /** Vertical radio group: one row per option in `rect`.
     *  @param {string} id @param {Rect} rect @param {ChoiceOption[]} options @param {string | null} value
     *  @param {{ label?: string }} [settings] @returns {string | null} */
    function radio(id, rect, options, value, settings = {}) {
        const theme = t(), px = ctx.px, size = ctx.sp(theme.font);
        const entries = normalize(options);
        const rows = ui.rows(rect, entries.map(() => ({ weight: 1 })), px(4));
        let next = value;
        entries.forEach((option, index) => {
            const row = rows[index];
            const selected = option.value === value;
            const state = option.disabled
                ? { hovered: false, held: false, clicked: false, id: ctx.claim(id + ':' + index) }
                : ctx.interact(id + ':' + index, row, {
                    a11y: { role: 'radio', label: (settings.label ? settings.label + ': ' : '') + option.label, checked: selected },
                });
            if (state.clicked && !selected) {
                next = option.value;
                ui.haptic('selection');
            }
            const on = ctx.springAt(state.id + '#on', next === option.value ? 1 : 0, springs.bouncy);
            const circle = { x: row.x + px(2), y: row.y + (row.height - px(22)) / 2, width: px(22), height: px(22) };
            ctx.fill(circle, ctx.colorAt(state.id + '#bg', state.hovered ? theme.raised : theme.surface), px(11));
            ctx.outline(circle, ctx.colorAt(state.id + '#ring', next === option.value ? theme.accent : theme.border),
                px(2), px(11));
            if (on > 0.01) {
                const dot = px(10) * on;
                ctx.fill({ x: circle.x + (circle.width - dot) / 2, y: circle.y + (circle.height - dot) / 2, width: dot, height: dot },
                    theme.accent, dot / 2);
            }
            ctx.text(option.label, row.x + px(34), row.y + row.height / 2 + size * 0.35, size,
                option.disabled ? theme.muted : theme.ink);
        });
        return next;
    }

    /** Segmented control: options side by side in a pill; a thumb slides to the
     *  selected one. @param {string} id @param {Rect} rect @param {ChoiceOption[]} options
     *  @param {string | null} value @param {{ label?: string }} [settings] @returns {string | null} */
    function segmented(id, rect, options, value, settings = {}) {
        const theme = t(), px = ctx.px, size = ctx.sp(theme.small || theme.font);
        const entries = normalize(options);
        const key = ctx.fullId(id);
        const track = rect;
        ctx.fill(track, theme.raised, rect.height / 2);
        const inner = ui.inset(track, px(3));
        const cells = ui.columns(inner, entries.map(() => ({ weight: 1 })), 0);
        let next = value;
        const states = entries.map((option, index) => {
            const state = option.disabled
                ? { hovered: false, held: false, clicked: false, id: ctx.claim(id + ':' + index) }
                : ctx.interact(id + ':' + index, cells[index], {
                    a11y: { role: 'tab', label: (settings.label ? settings.label + ': ' : '') + option.label, selected: option.value === value },
                });
            if (state.clicked && option.value !== value) {
                next = option.value;
                ui.haptic('selection');
            }
            return state;
        });
        const index = Math.max(0, entries.findIndex(option => option.value === next));
        if (entries.length) {
            const thumb = ctx.rectAt(key + '#thumb', cells[index], springs.snappy);
            const press = ctx.springAt(key + '#press', states[index]?.held ? 1 : 0, springs.snappy);
            const inset = px(1.5) * press;
            ctx.fill({ x: thumb.x + inset, y: thumb.y + inset, width: thumb.width - inset * 2, height: thumb.height - inset * 2 },
                theme.surface, (inner.height - inset * 2) / 2);
        }
        entries.forEach((option, i) => {
            const cell = cells[i];
            const label = ctx.truncate(option.label, cell.width - px(12), size);
            const ink = option.disabled ? theme.muted : i === index ? theme.ink : states[i].hovered ? theme.ink : theme.muted;
            ctx.text(label, cell.x + (cell.width - ctx.measure(label, size)) / 2, cell.y + cell.height / 2 + size * 0.35,
                size, ctx.colorAt(states[i].id + '#ink', ink));
        });
        return next;
    }

    /** Tabs: labels in a row with an underline that glides to the selected tab.
     *  The app draws the selected tab's content below.
     *  @param {string} id @param {Rect} rect @param {ChoiceOption[]} options @param {string | null} value
     *  @returns {string | null} */
    function tabs(id, rect, options, value) {
        const theme = t(), px = ctx.px, size = ctx.sp(theme.font);
        const entries = normalize(options);
        const key = ctx.fullId(id);
        // Tabs size to their labels, like text.
        const widths = entries.map(option => ctx.measure(option.label, size) + px(32));
        let x = rect.x;
        let next = value;
        const cells = entries.map((option, i) => {
            const cell = { x, y: rect.y, width: widths[i], height: rect.height };
            x += widths[i];
            const state = option.disabled
                ? { hovered: false, held: false, clicked: false, id: ctx.claim(id + ':' + i) }
                : ctx.interact(id + ':' + i, cell, { a11y: { role: 'tab', label: option.label, selected: option.value === value } });
            if (state.clicked && option.value !== value) next = option.value;
            const selected = option.value === next;
            ctx.fill(cell, ctx.colorAt(state.id + '#bg', state.hovered ? withAlpha(theme.raised, 0.8) : withAlpha(theme.raised, 0)));
            ctx.text(option.label, cell.x + px(16), cell.y + cell.height / 2 + size * 0.35, size,
                ctx.colorAt(state.id + '#ink', option.disabled ? theme.muted : selected ? theme.accent : theme.ink));
            return cell;
        });
        ctx.fill({ x: rect.x, y: rect.y + rect.height - px(1), width: rect.width, height: px(1) }, theme.border, 0);
        const index = entries.findIndex(option => option.value === next);
        if (index >= 0) {
            const bar = ctx.rectAt(key + '#bar', { x: cells[index].x + px(10), y: rect.y + rect.height - px(3),
                width: cells[index].width - px(20), height: px(3) }, springs.snappy);
            ctx.fill(bar, theme.accent, px(1.5));
        }
        return next;
    }

    Object.assign(ui, { radio, segmented, tabs });
}
