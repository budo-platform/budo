/** Dropdown select, drawn in the overlay layer. */

import { springs } from '../motion.js';
import { withAlpha } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {string | { value: string, label?: string }} SelectOption */
/** @typedef {{ placeholder?: string, maxVisible?: number, label?: string, enabled?: boolean }} SelectOptions */

const KEY_ESCAPE = 41;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;
    /** @type {Set<string>} */
    const open = new Set();
    /** Picks made in the menu: the menu draws in the overlay at ui.end(), after
     *  select() returned, so the pick is returned by the next frame's call.
     *  @type {Map<string, string>} */
    const pending = new Map();

    /** Pick one of `options`. Returns the (possibly new) value.
     *  @param {string} id @param {Rect} rect @param {SelectOption[]} options
     *  @param {string | null} value @param {SelectOptions} [settings] @returns {string | null} */
    function select(id, rect, options, value, settings = {}) {
        const theme = t();
        const px = ctx.px;
        const key = ctx.fullId(id);
        const shown = options.map(option => typeof option === 'string' ? option : option.label || option.value);
        const index = options.findIndex(option => (typeof option === 'string' ? option : option.value) === value);
        const enabled = settings.enabled !== false;
        if (!enabled) open.delete(key);
        const state = ctx.interact(id, rect, {
            cursor: enabled ? 'pointer' : null, focusable: enabled,
            a11y: {
                role: 'combobox', label: settings.label || settings.placeholder || '',
                value: index >= 0 ? shown[index] : '', expanded: open.has(key), disabled: !enabled,
            },
        });
        if (!enabled) state.clicked = false;
        if (pending.has(key)) {
            value = pending.get(key);
            pending.delete(key);
        }
        const entries = options.map(option => typeof option === 'string'
            ? { value: option, label: option } : { value: option.value, label: option.label || option.value });
        const current = entries.find(option => option.value === value);
        if (state.clicked) {
            if (open.has(key)) open.delete(key);
            else {
                open.add(key);
                // Opened from the keyboard: focus the current option.
                if (ctx.focusVisible && state.focused) ctx.pendingFocus = ctx.fullId(id + ':option:' + Math.max(0, index));
            }
        }
        const isOpen = open.has(key);

        // Trigger: a field-like box with the value and a chevron.
        const size = ctx.sp(theme.font);
        ctx.fill(rect, ctx.colorAt(key + '#bg', state.hovered && enabled ? theme.raised : theme.surface));
        ctx.outline(rect, ctx.colorAt(key + '#ring', isOpen ? theme.focus : theme.border), isOpen ? 2 : 1);
        const textWidth = rect.width - px(48);
        ctx.text(ctx.truncate(current ? current.label : settings.placeholder || '', textWidth, size),
            rect.x + px(12), rect.y + rect.height / 2 + size * 0.35, size, current && enabled ? theme.ink : theme.muted);
        const flip = 1 - 2 * ctx.springAt(key + '#chevron', isOpen ? 1 : 0, springs.snappy);
        const cx = rect.x + rect.width - px(22), cy = rect.y + rect.height / 2;
        ctx.line(cx - px(6), cy - px(3) * flip, cx, cy + px(3) * flip, theme.muted, px(2));
        ctx.line(cx, cy + px(3) * flip, cx + px(6), cy - px(3) * flip, theme.muted, px(2));

        const rowHeight = px(theme.row) - px(4);
        const visibleCount = Math.min(entries.length, settings.maxVisible || 7);
        const menuHeight = visibleCount * rowHeight + px(8);
        const bounds = ctx.bounds;
        const below = rect.y + rect.height + px(4);
        const above = rect.y - px(4) - menuHeight;
        const placeBelow = below + menuHeight <= bounds.y + bounds.height || above < bounds.y;
        const menu = { x: rect.x, y: placeBelow ? below : above, width: rect.width, height: menuHeight };

        ui.overlay(() => ui.presence(key + '#menu', isOpen, appear => {
            const lift = (placeBelow ? -1 : 1) * px(8) * (1 - appear);
            // The shadow fades outside the frosted layer so it does not get blurred into it.
            ctx.shadow({ ...menu, y: menu.y + lift }, 2, undefined, appear);
            ui.layer(menu, { opacity: appear, y: lift, scale: 0.97 + 0.03 * appear, backdrop: px(18) }, () => {
                ctx.fill(menu, ctx.canvasHas('saveLayer') ? withAlpha(theme.surface, 0.8) : theme.surface);
                ctx.outline(menu, theme.border, 1);
                const inner = ui.inset(menu, px(4));
                ui.scroll(id + ':menu', inner, entries.length * rowHeight, content => {
                    entries.forEach((option, index) => {
                        const row = { x: content.x, y: content.y + index * rowHeight, width: content.width, height: rowHeight };
                        if (ui.choice(id + ':option:' + index, option.label, row, option.value === value) && isOpen) {
                            pending.set(key, option.value);
                            open.delete(key);
                            ui.haptic('selection');
                            if (ctx.focusVisible) ctx.pendingFocus = key;
                        }
                    });
                });
            });
            // Close on a press outside the menu and the trigger, or on Escape.
            if (isOpen && ((ctx.input.pointer.pressed && !ctx.inside(menu) && !ctx.inside(rect)) ||
                sys.input.isKeyPressed(KEY_ESCAPE))) {
                open.delete(key);
                if (ctx.focusVisible) ctx.pendingFocus = key;
            }
        }));
        return value;
    }

    Object.assign(ui, { select });
}
