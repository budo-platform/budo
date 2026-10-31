/** Keyboard focus: Tab / Shift+Tab (and Up / Down) move through the
 *  focusable widgets in draw order, Enter or Space activates the focused one,
 *  and an animated ring shows where focus is after keyboard use (like CSS
 *  :focus-visible). Scroll areas reveal the widget focus moves into. */

import { springs } from './motion.js';
import { intersect } from './layout.js';
import { withAlpha } from './color.js';

/** @typedef {import('./layout.js').Rect} Rect */

// USB HID scancodes
const KEY_TAB = 43, KEY_ENTER = 40, KEY_SPACE = 44, KEY_DOWN = 81, KEY_UP = 82;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    /** @type {string | null} widget with the keyboard focus */
    ctx.keyFocus = null;
    /** the ring is shown (focus last moved by keyboard) */
    ctx.focusVisible = false;
    /** @type {{ key: string, rect: Rect, full: Rect, scroll: string | null, kind: string }[]} */
    ctx.focusables = [];
    /** @type {string[]} scroll areas being drawn */
    ctx.scrollStack = [];
    /** @type {{ scroll: string, rect: Rect } | null} scroll this rectangle into view next frame */
    ctx.reveal = null;
    /** @type {string | null} take the keyboard focus next frame */
    ctx.pendingFocus = null;
    /** @type {number | null} index in focusables where an open modal's widgets start */
    ctx.focusTrap = null;
    /** a modal was open last frame: the main layer ignores wheel and touch scrolling */
    ctx.modalOpen = false;
    let modalThisFrame = false;
    let missingFrames = 0;
    /** @type {Rect | null} */
    let ringRect = null;

    /** @param {number} scancode */
    const pressed = scancode => typeof sys.input?.isKeyPressed === 'function' && sys.input.isKeyPressed(scancode);
    ctx.keyPressed = pressed;

    /** @type {Map<number, number>} when each repeating key went down (and, at -key, repeats fired) */
    const held = new Map();
    /** Pressed this frame, or held long enough to repeat: after 0.45 s, every
     *  35 ms (desktop SDL and the browser report one press per key down).
     *  @param {number} scancode */
    ctx.keyRepeated = scancode => {
        const now = ctx.input.totalTime || 0;
        if (pressed(scancode)) {
            held.set(scancode, now);
            held.delete(-scancode);
            return true;
        }
        const since = held.get(scancode);
        if (since === undefined) return false;
        if (typeof sys.input.isKeyDown !== 'function' || !sys.input.isKeyDown(scancode)) {
            held.delete(scancode);
            held.delete(-scancode);
            return false;
        }
        ctx.animating = true;
        if (now - since < 0.45) return false;
        const count = Math.floor((now - since - 0.45) / 0.035);
        if (count < (held.get(-scancode) || 0)) return false;
        held.set(-scancode, count + 1);
        return true;
    };

    /** A widget that uses the arrow keys itself (a text area) has the focus this frame. */
    ctx.arrowsTaken = false;

    ctx.beginFocus = () => {
        ctx.arrowsTaken = false;
        ctx.focusables = [];
        ctx.scrollStack = [];
        ctx.focusTrap = null;
        ctx.modalOpen = modalThisFrame;
        modalThisFrame = false;
        if (ctx.pendingFocus !== null) {
            ctx.keyFocus = ctx.pendingFocus;
            ctx.focusVisible = true;
            ctx.pendingFocus = null;
        }
    };

    /** Register a focusable widget (in draw order); returns whether it has focus.
     *  @param {string} key @param {Rect} rect @param {string} kind */
    ctx.focusable = (key, rect, kind) => {
        const full = ctx.toScreen(rect);
        ctx.focusables.push({
            key, full, kind,
            rect: ctx.clip ? intersect(full, ctx.clip) : full,
            scroll: ctx.scrollStack.length ? ctx.scrollStack[ctx.scrollStack.length - 1] : null,
        });
        return ctx.keyFocus === key;
    };

    /** Enter or Space on the focused widget (text fields handle their own keys). @param {string} key */
    ctx.activated = key => ctx.keyFocus === key && (pressed(KEY_ENTER) || pressed(KEY_SPACE));

    /** Give `key` the keyboard focus (shows the ring). @param {string} key */
    ctx.focusKey = key => {
        ctx.keyFocus = key;
        ctx.focusVisible = true;
    };

    /** Keep the keyboard focus inside the modal whose widgets are drawn next
     *  (dialogs, sheets): call before drawing its content. */
    ctx.trapFocus = () => {
        ctx.focusTrap = ctx.focusables.length;
        modalThisFrame = true;
    };

    ctx.endFocus = () => {
        const input = ctx.input;
        if (input.pointer.pressed) ctx.focusVisible = false;
        const list = ctx.focusTrap === null ? ctx.focusables : ctx.focusables.slice(ctx.focusTrap);
        let index = list.findIndex(item => item.key === ctx.keyFocus);
        // A modal just opened: move the focus into it.
        if (ctx.focusTrap !== null && index < 0 && list.length) {
            ctx.keyFocus = list[0].key;
            index = 0;
        }
        // Forget focus on a widget that stopped being drawn (after one frame of grace).
        if (index >= 0) missingFrames = 0;
        else if (ctx.keyFocus !== null && ctx.pendingFocus === null && ++missingFrames > 1) {
            ctx.keyFocus = null;
            missingFrames = 0;
        }
        let step = 0;
        if (pressed(KEY_TAB)) step = input.keyboard?.shift ? -1 : 1;
        else if (index >= 0 && !ctx.arrowsTaken && pressed(KEY_DOWN)) step = 1;
        else if (index >= 0 && !ctx.arrowsTaken && pressed(KEY_UP)) step = -1;
        if (step && list.length) {
            index = index < 0 ? (step > 0 ? 0 : list.length - 1) : (index + step + list.length) % list.length;
            const next = list[index];
            ctx.focusKey(next.key);
            if (next.scroll) ctx.reveal = { scroll: next.scroll, rect: next.full };
        }
        drawRing(index >= 0 ? list[index] : null);
    };

    /** The ring glides to the focused widget and fades in and out. */
    function drawRing(current) {
        const px = ctx.px;
        const visible = ctx.focusVisible && !!current && current.kind !== 'textbox' &&
            current.rect.width > 0 && current.rect.height > 0;
        if (visible) ringRect = current.rect;
        const alpha = ctx.springAt('focus#alpha', visible ? 1 : 0, springs.snappy);
        if (alpha < 0.01 || !ringRect) return;
        const gap = px(3);
        const target = { x: ringRect.x - gap, y: ringRect.y - gap, width: ringRect.width + gap * 2, height: ringRect.height + gap * 2 };
        const ring = ctx.rectAt('focus#ring', target, springs.snappy);
        ctx.outline(ring, withAlpha(ctx.theme.focus, alpha), px(2), px(ctx.theme.radius) + gap);
    }

    /** Move the keyboard focus to `id` (in the current scope) and show the ring. @param {string} id */
    ui.focusOn = id => ctx.focusKey(ctx.fullId(id));

    Object.defineProperties(ui, {
        /** Full id of the widget with the keyboard focus, or null. */
        keyFocus: { get: () => ctx.keyFocus },
    });
}
