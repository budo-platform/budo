/** Surfaces above the content: menus, context menus, tooltips, dialogs,
 *  bottom sheets, and toasts. All animate in and out, draw on frosted glass,
 *  and work with the pointer, the keyboard, and screen readers. */

import { clamp } from '../math.js';
import { springs } from '../motion.js';
import { withAlpha } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {{ value: string, label: string, disabled?: boolean, danger?: boolean }} MenuItem */
/** @typedef {{ title?: string, width?: number, height?: number }} DialogOptions */

// USB HID scancodes
const KEY_ESCAPE = 41;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;
    /** @type {Map<string, { x: number, y: number }>} open menus by key, anchored at a point */
    const openMenus = new Map();
    /** @type {Map<string, string>} picks made in a menu, returned next frame */
    const picks = new Map();
    /** @type {Map<string, number>} hover start times for tooltips */
    const hoverSince = new Map();
    /** @type {Map<string, { since: number, x: number, y: number }>} touch long presses */
    const longPresses = new Map();
    /** @type {Set<string>} modals the user asked to close */
    const dismissals = new Set();
    /** @type {Map<string, string | null>} focus to restore when a modal closes */
    const restoreFocus = new Map();
    /** @type {{ id: number, text: string, until: number, kind: string }[]} */
    let toasts = [];
    let nextToast = 1;

    // ── Menus ──────────────────────────────────────────────────────────────

    /** The menu panel of `id` (full id `key`), anchored below (or above) `anchor`. */
    function menuPanel(id, key, anchor, items, isOpen) {
        const theme = t(), px = ctx.px, size = ctx.sp(theme.font);
        const rowHeight = px(theme.row) - px(6);
        const width = Math.max(px(180), ...items.map(item => ctx.measure(item.label, size) + px(40)));
        const height = items.length * rowHeight + px(8);
        const bounds = ctx.bounds;
        const below = anchor.y + anchor.height + px(4);
        const placeBelow = below + height <= bounds.y + bounds.height || anchor.y - height - px(4) < bounds.y;
        const panel = {
            x: clamp(anchor.x, bounds.x + px(4), bounds.x + bounds.width - width - px(4)),
            y: placeBelow ? below : anchor.y - height - px(4), width, height,
        };
        ui.overlay(() => ui.presence(key + '#menu', isOpen, appear => {
            const radius = px(theme.radius + 3);
            ctx.shadow(panel, 2, radius, appear);
            ui.layer(panel, { opacity: appear, y: (placeBelow ? -1 : 1) * px(6) * (1 - appear),
                scale: 0.97 + 0.03 * appear, backdrop: px(18), radius }, () => {
                ctx.fill(panel, ctx.canvasHas('saveLayer') ? withAlpha(theme.surface, 0.82) : theme.surface, radius);
                ctx.outline(panel, theme.border, 1, radius);
                items.forEach((item, index) => {
                    const row = { x: panel.x + px(4), y: panel.y + px(4) + index * rowHeight, width: width - px(8), height: rowHeight };
                    const state = item.disabled
                        ? { hovered: false, clicked: false, id: ctx.claim(id + ':item:' + index) }
                        : ctx.interact(id + ':item:' + index, row, { a11y: { role: 'option', label: item.label } });
                    ctx.fill(row, ctx.colorAt(state.id + '#bg', state.hovered ? theme.raised : withAlpha(theme.raised, 0)), px(theme.radius));
                    ctx.text(item.label, row.x + px(12), row.y + row.height / 2 + size * 0.35, size,
                        item.disabled ? theme.muted : item.danger ? theme.danger : theme.ink);
                    if (state.clicked && isOpen) {
                        picks.set(key, item.value);
                        openMenus.delete(key);
                        ui.haptic('selection');
                    }
                });
            });
            if (isOpen && ((ctx.input.pointer.pressed && !ctx.inside(panel) && !ctx.inside(anchor)) ||
                ctx.keyPressed(KEY_ESCAPE))) openMenus.delete(key);
        }));
    }

    /** A button that opens a menu of actions. Returns the picked item's value
     *  (the frame after the pick), or null.
     *  @param {string} id @param {Rect} rect @param {string} label @param {MenuItem[]} items @returns {string | null} */
    function menu(id, rect, label, items) {
        const theme = t(), px = ctx.px, size = ctx.sp(theme.font);
        const key = ctx.fullId(id);
        const isOpen = openMenus.has(key);
        const state = ctx.interact(id, rect, { a11y: { role: 'button', label, expanded: isOpen } });
        if (state.clicked) {
            if (isOpen) openMenus.delete(key);
            else {
                openMenus.set(key, { x: rect.x, y: rect.y });
                if (ctx.focusVisible && state.focused) ctx.pendingFocus = ctx.fullId(id + ':item:0');
            }
        }
        ctx.fill(rect, ctx.colorAt(key + '#bg', state.held ? theme.border : state.hovered || isOpen ? theme.raised : theme.surface));
        ctx.outline(rect, theme.border, 1);
        ctx.text(label, rect.x + px(14), rect.y + rect.height / 2 + size * 0.35, size, theme.ink);
        const cx = rect.x + rect.width - px(18), cy = rect.y + rect.height / 2;
        const flip = 1 - 2 * ctx.springAt(key + '#chevron', isOpen ? 1 : 0, springs.snappy);
        ctx.line(cx - px(5), cy - px(2.5) * flip, cx, cy + px(2.5) * flip, theme.muted, px(2));
        ctx.line(cx, cy + px(2.5) * flip, cx + px(5), cy - px(2.5) * flip, theme.muted, px(2));
        menuPanel(id, key, rect, items, openMenus.has(key));
        return takePick(key);
    }

    /** Return a pick made last frame, moving focus back to the trigger after keyboard use. */
    function takePick(key) {
        if (!picks.has(key)) return null;
        const value = picks.get(key);
        picks.delete(key);
        if (ctx.focusVisible) ctx.pendingFocus = key;
        return value;
    }

    /** A menu that opens where the user right-clicks (or long-presses) inside
     *  `rect`. Returns the picked value (the frame after the pick), or null.
     *  @param {string} id @param {Rect} rect @param {MenuItem[]} items @returns {string | null} */
    function contextMenu(id, rect, items) {
        const key = ctx.fullId(id);
        const input = ctx.input, pointer = input.pointer;
        let opened = null;
        if (input.mouse?.rightPressed && ctx.inside(rect)) opened = { x: pointer.x, y: pointer.y };
        // Touch: hold still for half a second.
        if (pointer.type === 'touch') {
            const now = input.totalTime || 0;
            const press = longPresses.get(key);
            if (pointer.pressed && ctx.inside(rect)) longPresses.set(key, { since: now, x: pointer.x, y: pointer.y });
            else if (press && (!pointer.down || Math.hypot(pointer.x - press.x, pointer.y - press.y) > ctx.px(10)))
                longPresses.delete(key);
            else if (press && now - press.since >= 0.5) {
                opened = { x: press.x, y: press.y };
                longPresses.delete(key);
                ctx.active = null;
                ui.haptic('heavy');
            } else if (press) ui.wakeAfter(0.5 - (now - press.since));
        }
        if (opened) {
            openMenus.set(key, opened);
            anchors.set(key, opened);
        }
        // Keep drawing after it closes, so it can animate out.
        const at = anchors.get(key);
        if (at) menuPanel(id, key, { x: at.x, y: at.y, width: 0, height: 0 }, items, openMenus.has(key));
        return takePick(key);
    }
    /** @type {Map<string, { x: number, y: number }>} where each context menu last opened */
    const anchors = new Map();

    // ── Tooltips ───────────────────────────────────────────────────────────

    /** Show `text` near `rect` after the pointer rests on it for a moment, or
     *  while the widget in `rect` has the keyboard focus. Call after drawing
     *  the widget. @param {string} id @param {Rect} rect @param {string} text */
    function tooltip(id, rect, text) {
        const theme = t(), px = ctx.px, size = ctx.sp(theme.small || theme.font);
        const key = 'tooltip:' + ctx.fullId(id);
        const input = ctx.input, now = input.totalTime || 0;
        const hovering = input.pointer.type !== 'touch' && !input.pointer.down && ctx.inside(rect);
        if (hovering && !hoverSince.has(key)) hoverSince.set(key, now);
        if (!hovering) hoverSince.delete(key);
        const waited = hovering ? now - hoverSince.get(key) : 0;
        if (hovering && waited < 0.6) ui.wakeAfter(0.6 - waited);
        const screen = ctx.toScreen(rect);
        const focused = ctx.focusVisible && ctx.focusables.some(item => item.key === ctx.keyFocus &&
            Math.abs(item.full.x - screen.x) < 1 && Math.abs(item.full.y - screen.y) < 1);
        const visible = (hovering && waited >= 0.6) || focused;
        const width = ctx.measure(text, size) + px(20), height = size + px(14);
        const above = screen.y - height - px(6) >= ctx.bounds.y;
        const bubble = {
            x: clamp(screen.x + (screen.width - width) / 2, ctx.bounds.x + px(4), ctx.bounds.x + ctx.bounds.width - width - px(4)),
            y: above ? screen.y - height - px(6) : screen.y + screen.height + px(6), width, height,
        };
        ui.overlay(() => ui.presence(key, visible, appear => {
            ui.layer(bubble, { opacity: appear, y: (above ? 1 : -1) * px(4) * (1 - appear), scale: 0.94 + 0.06 * appear }, () => {
                ctx.fill(bubble, withAlpha(theme.ink, 0.9), height / 2);
                ctx.text(text, bubble.x + px(10), bubble.y + bubble.height / 2 + size * 0.35, size, theme.surface);
            });
        }, springs.snappy));
    }

    // ── Dialogs and sheets ─────────────────────────────────────────────────

    /** The scrim behind modals; returns whether it was pressed. */
    function scrim(id, appear, blocking) {
        const bounds = ctx.bounds;
        const state = ctx.interact(id + '#scrim', bounds, { focusable: false, cursor: null });
        ui.layer(bounds, { opacity: appear, backdrop: ctx.px(6), radius: 0 }, () => {
            ctx.fill(bounds, withAlpha('#000000', 0.32), 0);
        });
        return blocking && state.clicked;
    }

    /** Remember the focus to restore, and restore it after the modal closes. */
    function modalFocus(key, open) {
        if (open && !restoreFocus.has(key)) restoreFocus.set(key, ctx.keyFocus);
        if (!open && restoreFocus.has(key)) {
            const previous = restoreFocus.get(key);
            restoreFocus.delete(key);
            if (previous && ctx.focusVisible) ctx.pendingFocus = previous;
        }
    }

    /** A modal dialog centered on the screen. `draw(content)` draws its body.
     *  Returns true (the frame after) when the user asks to close it (Escape or
     *  a press outside); the app then sets `open` to false.
     *  @param {string} id @param {boolean} open @param {(content: Rect) => void} draw
     *  @param {DialogOptions} [options] @returns {boolean} */
    function dialog(id, open, draw, options = {}) {
        const key = ctx.fullId(id);
        const theme = t(), px = ctx.px;
        modalFocus(key, open);
        // The overlay draws at ui.end(), after this returns: report a dismissal next frame.
        const dismissed = dismissals.delete(key);
        ui.overlay(() => ui.presence(key + '#dialog', open, appear => {
            const bounds = ctx.bounds;
            const width = Math.min(options.width || px(440), bounds.width - px(32));
            const height = Math.min(options.height || px(260), bounds.height - px(32));
            const panel = { x: bounds.x + (bounds.width - width) / 2, y: bounds.y + (bounds.height - height) / 2, width, height };
            if (scrim(id, appear, open)) dismissals.add(key);
            const radius = px(theme.radius + 8);
            ctx.shadow(panel, 3, radius, appear);
            ui.layer(panel, { opacity: appear, scale: 0.94 + 0.06 * appear, y: px(12) * (1 - appear) }, () => {
                ctx.interact(id + '#panel', panel, { focusable: false, cursor: null });
                ctx.fill(panel, theme.surface, radius);
                ctx.describe(key, panel, { role: 'group', label: options.title || '' });
                let content = ui.inset(panel, px(20));
                if (options.title) {
                    const size = ctx.sp(theme.font + 4);
                    ctx.text(options.title, content.x, content.y + size, size, theme.ink);
                    content = { ...content, y: content.y + size * 1.8, height: content.height - size * 1.8 };
                }
                if (open) ctx.trapFocus();
                draw(content);
            });
            if (open && ctx.keyPressed(KEY_ESCAPE)) dismissals.add(key);
        }));
        return dismissed;
    }

    /** A sheet that slides up from the bottom; drag its handle down (or press
     *  outside, or Escape) to dismiss. Returns true (the frame after) when the user asks to close.
     *  @param {string} id @param {boolean} open @param {(content: Rect) => void} draw
     *  @param {{ height?: number }} [options] @returns {boolean} */
    function sheet(id, open, draw, options = {}) {
        const key = ctx.fullId(id);
        const theme = t(), px = ctx.px;
        modalFocus(key, open);
        // The overlay draws at ui.end(), after this returns: report a dismissal next frame.
        const dismissed = dismissals.delete(key);
        ui.overlay(() => ui.presence(key + '#sheet', open, appear => {
            const bounds = ctx.bounds;
            const height = Math.min(options.height || bounds.height * 0.5, bounds.height - px(24));
            const width = Math.min(bounds.width, px(720));
            const rest = { x: bounds.x + (bounds.width - width) / 2, y: bounds.y + bounds.height - height, width, height: height + px(24) };
            if (scrim(id, appear, open)) dismissals.add(key);
            // Dragging the handle moves the sheet; letting go far enough (or fast) dismisses it.
            const handle = { x: rest.x, y: rest.y, width, height: px(28) };
            const drag = ctx.interact(id + '#handle', handle, { cursor: 'grab', focusable: false });
            const pulled = drag.held ? Math.max(0, ctx.input.pointer.y - (dragStarts.get(key) ?? ctx.input.pointer.y)) : 0;
            if (drag.held && !dragStarts.has(key)) dragStarts.set(key, ctx.input.pointer.y);
            if (!drag.held && dragStarts.has(key)) {
                const released = lastPull.get(key) || 0;
                if (released > height * 0.3 || (ctx.input.pointer.dy || 0) > px(18)) dismissals.add(key);
                dragStarts.delete(key);
            }
            lastPull.set(key, pulled);
            const offset = drag.held ? pulled : ctx.springAt(key + '#pull', 0, springs.snappy);
            if (drag.held) ctx.snapAt(key + '#pull', pulled);
            const y = rest.y + (1 - appear) * (height + px(24)) + offset;
            const panel = { ...rest, y };
            const radius = px(theme.radius + 10);
            ctx.shadow(panel, 3, radius, appear);
            ctx.interact(id + '#panel', panel, { focusable: false, cursor: null });
            ctx.fill(panel, theme.surface, radius);
            ctx.fill({ x: panel.x + (width - px(40)) / 2, y: panel.y + px(10), width: px(40), height: px(5) }, theme.border, px(2.5));
            ctx.describe(key, panel, { role: 'group', label: options.title || '' });
            if (open) ctx.trapFocus();
            draw({ x: panel.x + px(20), y: panel.y + px(30), width: width - px(40), height: height - px(40) });
            if (open && ctx.keyPressed(KEY_ESCAPE)) dismissals.add(key);
        }));
        return dismissed;
    }
    /** @type {Map<string, number>} */
    const dragStarts = new Map();
    /** @type {Map<string, number>} */
    const lastPull = new Map();

    // ── Toasts ─────────────────────────────────────────────────────────────

    /** Show a short message at the bottom of the screen. It stacks with other
     *  toasts and leaves after `duration` seconds.
     *  @param {string} text @param {{ duration?: number, kind?: 'info' | 'success' | 'danger' }} [options] */
    function toast(text, options = {}) {
        const now = ctx.input?.totalTime || 0;
        toasts.push({ id: nextToast++, text, until: now + (options.duration || 2.5), kind: options.kind || 'info' });
        if (options.kind === 'danger') ui.haptic('error');
        else if (options.kind === 'success') ui.haptic('success');
    }

    /** Draw the toasts (called by ui.end, below the focus ring). */
    ctx.drawToasts = () => {
        const theme = t(), px = ctx.px, size = ctx.sp(theme.font - 2);
        const now = ctx.input.totalTime || 0;
        const bounds = ctx.bounds;
        let bottom = bounds.y + bounds.height - px(20);
        const kept = [];
        for (const item of toasts.slice().reverse()) {
            const visible = now < item.until;
            const width = Math.min(ctx.measure(item.text, size) + px(48), bounds.width - px(24));
            const height = px(46);
            const target = { x: bounds.x + (bounds.width - width) / 2, y: bottom - height, width, height };
            const rect = { ...target, y: ctx.springAt('toast#y:' + item.id, target.y, springs.default) };
            const drawn = ui.presence('toast:' + item.id, visible, appear => {
                const ink = item.kind === 'danger' ? theme.danger : item.kind === 'success' ? theme.accent : theme.ink;
                ctx.shadow({ ...rect, y: rect.y + (1 - appear) * px(20) }, 1, height / 2, appear);
                ui.layer(rect, { opacity: appear, y: (1 - appear) * px(20), scale: 0.94 + 0.06 * appear,
                    backdrop: px(16), radius: height / 2 }, () => {
                    ctx.fill(rect, withAlpha(ink, 0.86), height / 2);
                    ctx.describeText(rect, item.text);
                    ctx.text(item.text, rect.x + (rect.width - ctx.measure(item.text, size)) / 2,
                        rect.y + rect.height / 2 + size * 0.35, size, theme.surface);
                });
            });
            if (visible) {
                ui.wakeAfter(item.until - now);
                bottom -= height + px(10);
            }
            if (visible || drawn) kept.push(item);
        }
        toasts = kept.reverse();
    };

    Object.assign(ui, { menu, contextMenu, tooltip, dialog, sheet, toast });
}
