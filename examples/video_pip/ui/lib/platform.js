/** System integration: user preferences, mouse cursors, accessibility,
 *  haptics, and frame scheduling. Every runtime feature is optional; on
 *  older runtimes (and in tests) the matching behavior is simply off. */

/** @typedef {import('./layout.js').Rect} Rect */
/** @typedef {{ darkMode: boolean, reducedMotion: boolean, highContrast: boolean, fontScale: number,
 *   safeArea: { top: number, right: number, bottom: number, left: number } }} SystemPreferences */
/** @typedef {{ role: string, label?: string, value?: string, checked?: boolean, selected?: boolean,
 *   disabled?: boolean, expanded?: boolean, min?: number, max?: number, rangeValue?: number }} AccessibilityInfo */

/** @type {SystemPreferences} */
const DEFAULT_PREFERENCES = {
    darkMode: false, reducedMotion: false, highContrast: false, fontScale: 1,
    safeArea: { top: 0, right: 0, bottom: 0, left: 0 },
};

/** @param {any} object @param {string} name */
const has = (object, name) => !!object && typeof object[name] === 'function';

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    /** @type {SystemPreferences} */
    ctx.system = DEFAULT_PREFERENCES;
    ctx.cursor = null;
    let appliedCursor = null;
    /** @type {any[]} */
    ctx.a11yNodes = [];
    /** @type {Map<string, { action: string, value: string }>} */
    ctx.a11yActions = new Map();
    let a11yActive = false;
    let a11yPublished = '';
    ctx.wake = Infinity;
    ctx.haptics = true;

    /** Text size: density-independent pixels scaled by the user's text size. @param {number} value */
    ctx.sp = value => value * ctx.density * (ctx.system.fontScale || 1);

    /** Called by ui.begin(). */
    ctx.beginPlatform = () => {
        const device = globalThis.sys?.device;
        ctx.system = has(device, 'getPreferences') ? device.getPreferences() || DEFAULT_PREFERENCES : DEFAULT_PREFERENCES;
        ctx.cursor = null;
        ctx.wake = Infinity;
        ctx.a11yNodes = [];
        ctx.a11yActions = new Map();
        const accessibility = globalThis.sys?.accessibility;
        // Runtimes cache this; it changes when a screen reader starts or stops.
        if (has(accessibility, 'isActive')) {
            const active = accessibility.isActive();
            if (active && !a11yActive) a11yPublished = ''; // republish for the new reader
            a11yActive = active;
        }
        ctx.a11yEnabled = a11yActive;
        if (a11yActive && has(accessibility, 'takeActions'))
            for (const action of accessibility.takeActions()) ctx.a11yActions.set(action.id, action);
    };

    /** Called by ui.end(). */
    ctx.endPlatform = () => {
        const device = globalThis.sys?.device;
        const touch = ctx.input?.pointer?.type === 'touch';
        const cursor = ctx.cursor || 'default';
        if (!touch && cursor !== appliedCursor && has(device, 'setCursor')) {
            device.setCursor(cursor);
            appliedCursor = cursor;
        }
        const accessibility = globalThis.sys?.accessibility;
        if (ctx.a11yEnabled && has(accessibility, 'update')) {
            const tree = JSON.stringify(ctx.a11yNodes);
            if (tree !== a11yPublished) {
                accessibility.update(ctx.a11yNodes);
                a11yPublished = tree;
            }
        }
    };

    /** Describe a widget for screen readers (no-op unless a screen reader runs).
     *  @param {string} key @param {Rect} rect @param {AccessibilityInfo} info */
    ctx.describe = (key, rect, info) => {
        if (!ctx.a11yEnabled || !info) return;
        const screen = ctx.toScreen(rect);
        ctx.a11yNodes.push({
            id: key, ...info, label: info.label || '',
            x: Math.round(screen.x), y: Math.round(screen.y),
            width: Math.round(screen.width), height: Math.round(screen.height),
            focused: ctx.focus === key || ctx.keyFocus === key,
        });
    };

    /** Describe static text (labels, paragraphs) for screen readers.
     *  @param {Rect} rect @param {string} text @param {string} [role] */
    ctx.describeText = (rect, text, role = 'text') => {
        if (ctx.a11yEnabled && text) ctx.describe('text:' + ctx.a11yNodes.length, rect, { role, label: String(text) });
    };

    /** The action a screen reader requested on `key` this frame, if any. @param {string} key */
    ctx.actionFor = key => ctx.a11yActions.get(key) || null;

    /** Haptic feedback on devices that have it; off with `ui.haptics = false`.
     *  @param {'light' | 'medium' | 'heavy' | 'selection' | 'success' | 'warning' | 'error'} [kind] */
    ui.haptic = (kind = 'light') => {
        const device = globalThis.sys?.device;
        return ctx.haptics && has(device, 'haptic') ? device.haptic(kind) : false;
    };

    /** Ask for a frame within `seconds` even if nothing moves (timers, blinking carets). @param {number} seconds */
    ui.wakeAfter = seconds => {
        ctx.wake = Math.min(ctx.wake, Math.max(0, seconds));
    };

    /** Schedule `frame` for the next frame when something animates, otherwise
     *  for the next input event (or `ui.wakeAfter` deadline): idle apps then
     *  draw nothing and save battery. Use instead of
     *  `sys.animation.requestFrame(frame)` after `ui.end()`.
     *  @param {(timestamp: number) => void} frame */
    ui.nextFrame = frame => {
        const animation = globalThis.sys.animation;
        if (ctx.animating || ctx.wake <= 0 || !has(animation, 'waitForInput'))
            return animation.requestFrame(frame);
        return animation.waitForInput(frame, Number.isFinite(ctx.wake) ? ctx.wake * 1000 : undefined);
    };

    Object.defineProperties(ui, {
        /** The user's system preferences (dark mode, reduced motion, text scale, safe areas). */
        system: { get: () => ctx.system },
        haptics: { get: () => ctx.haptics, set: value => { ctx.haptics = !!value; } },
    });
    ui.sp = ctx.sp;
}
