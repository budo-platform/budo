/// <reference path="../../budo.d.ts" />
/** The UI context: frame lifecycle, hit testing, ids, motion state, layers,
 *  overlays, and drawing helpers. Widgets are installed on top of it. */

import { clamp } from './math.js';
import { mixColors, withAlpha } from './color.js';
import { Spring, springs } from './motion.js';
import { themes } from './theme.js';
import { contains, inset, intersect, stack } from './layout.js';
import { install as installBasic } from './widgets/basic.js';
import { install as installContainers } from './widgets/containers.js';
import { install as installText } from './widgets/text.js';
import { install as installTextArea } from './widgets/textarea.js';
import { install as installData } from './widgets/data.js';
import { install as installSelect } from './widgets/select.js';
import { install as installContent } from './widgets/content.js';
import { install as installPlatform } from './platform.js';
import { install as installIcons } from './icons.js';
import { install as installFocus } from './focus.js';
import { install as installChoices } from './widgets/choices.js';
import { install as installOverlays } from './widgets/overlays.js';
import { install as installFeedback } from './widgets/feedback.js';
import { install as installNavigation } from './widgets/navigation.js';

/** @typedef {import('./layout.js').Rect} Rect */
/** @typedef {import('./layout.js').LayoutSize} LayoutSize */
/** @typedef {import('./color.js').Color} Color */
/** @typedef {import('./theme.js').UITheme} UITheme */
/** @typedef {import('./motion.js').SpringPreset} SpringPreset */
/** @typedef {{ hovered: boolean, held: boolean, clicked: boolean, id: string,
 *   action: { action: string, value: string } | null, focused?: boolean }} Interaction */
/** @typedef {import('./platform.js').AccessibilityInfo} AccessibilityInfo */
/** @typedef {{ cursor?: string | null, a11y?: AccessibilityInfo, focusable?: boolean }} InteractOptions */
/** @typedef {{ id: string, rect: Rect, z: number }} Hit */
/** @typedef {{ a: number, tx: number, ty: number }} Transform screen = local * a + t */
/** @typedef {{ opacity?: number, x?: number, y?: number, scale?: number,
 *   backdrop?: number, radius?: number }} LayerOptions */

/** Motion entries not touched for this many frames are forgotten. */
const MOTION_TTL_FRAMES = 90;

/** @param {Partial<UITheme>} [theme] */
export function createUI(theme = {}) {
    const ctx = {
        /** @type {UITheme} */
        theme: { ...themes.light, ...theme },
        /** @type {InputState | null} */
        input: null,
        /** @type {Rect} */
        bounds: { x: 0, y: 0, width: 0, height: 0 },
        pointerWasDown: false,
        pressStart: null,
        pointerReleased: false,
        tapped: false,
        releasedOn: null,
        keyboardInset: 0,
        keyboardOpened: false,
        keyboardChanging: false,
        /** @type {Rect | null} clip in screen coordinates */
        clip: null,
        frame: 0,
        dt: 1 / 60,
        density: 1,
        /** @type {Set<string>} */
        seen: new Set(),
        /** @type {Hit[]} */
        hits: [],
        /** @type {Hit[]} */
        lastHits: [],
        /** @type {string | null} topmost widget under the pointer (last frame's order) */
        hoverOwner: null,
        z: 0,
        /** @type {{ draw: () => void, scopes: string[] }[]} */
        overlays: [],
        /** @type {string | null} */
        active: null,
        /** @type {string | null} */
        focus: null,
        /** @type {string | null} */
        freshFocus: null,
        /** @type {string[]} */
        scopes: [],
        /** @type {Transform} */
        transform: { a: 1, tx: 0, ty: 0 },
        alpha: 1,
        /** @type {Map<string, any>} */
        motion: new Map(),
        animating: false,
        reducedMotion: false,
        /** @type {boolean | null} set by the app; null follows the system */
        reducedMotionOverride: null,
        /** widgets drawn now ignore input (leaving pages) */
        inert: false,
        /** follow the system dark mode with themes.light / themes.dark */
        systemTheme: false,
        /** @type {Map<string, number>} */
        measured: new Map(),
    };

    /** @type {any} */
    const ui = {};

    // ── Units, ids ─────────────────────────────────────────────────────────

    /** Density-independent pixels to physical canvas pixels. @param {number} value */
    ctx.px = value => value * ctx.density;

    /** @param {string} id @returns {string} */
    ctx.fullId = id => (ctx.scopes.length ? ctx.scopes.join('/') + '/' + id : String(id));

    // ── Transforms and clipping ────────────────────────────────────────────

    /** @param {Rect} rect @returns {Rect} */
    ctx.toScreen = rect => {
        const { a, tx, ty } = ctx.transform;
        return { x: rect.x * a + tx, y: rect.y * a + ty, width: rect.width * a, height: rect.height * a };
    };

    /** Is the pointer over `rect` (local coordinates) and inside the clip?
     *  @param {Rect} rect @returns {boolean} */
    ctx.inside = rect => {
        const { x, y } = ctx.input.pointer;
        if (ctx.clip && !contains(ctx.clip, x, y)) return false;
        const { a, tx, ty } = ctx.transform;
        return contains(rect, (x - tx) / a, (y - ty) / a);
    };

    /** Clip drawing and hit testing to `rect` until `popClip()`. With a
     *  radius, drawing is clipped to the rounded rectangle when the canvas
     *  supports it. @param {Rect} rect @param {number} [radius] */
    ctx.pushClip = (rect, radius = 0) => {
        const previous = ctx.clip;
        const screen = ctx.toScreen(rect);
        ctx.clip = previous ? intersect(screen, previous) : screen;
        sys.canvas.save();
        if (radius > 0 && canvasHas('clipRoundRect'))
            sys.canvas.clipRoundRect(rect.x, rect.y, rect.width, rect.height, radius, radius);
        else
            sys.canvas.clipRect(rect.x, rect.y, rect.width, rect.height);
        return previous;
    };
    /** @param {Rect | null} previous */
    ctx.popClip = previous => {
        sys.canvas.restore();
        ctx.clip = previous;
    };

    // ── Interaction ────────────────────────────────────────────────────────

    /** Register an id for this frame (duplicates are an app bug). @param {string} id @returns {string} */
    ctx.claim = id => {
        const key = ctx.fullId(id);
        if (ctx.seen.has(key)) throw new Error('Duplicate UI id: ' + key);
        ctx.seen.add(key);
        return key;
    };

    /** Hover, hold, and click state of a widget. Only the topmost widget under
     *  the pointer (by last frame's draw order, overlays above) is hovered.
     *  `cursor` (default "pointer") shows while hovered or held; `a11y`
     *  describes the widget to screen readers, whose "press" counts as a click.
     *  Widgets with `a11y` take the keyboard focus (unless `focusable: false`):
     *  Tab reaches them and Enter or Space clicks them.
     *  @param {string} id @param {Rect} rect @param {InteractOptions} [options] @returns {Interaction} */
    function interact(id, rect, options = {}) {
        const key = ctx.claim(id);
        // Inert content (a page leaving during a transition) ignores input.
        if (ctx.inert) return { hovered: false, held: false, clicked: false, id: key, action: null, focused: false };
        ctx.describe(key, rect, options.a11y);
        const action = ctx.actionFor(key);
        const kind = options.a11y?.role || 'button';
        const focusable = options.focusable === undefined ? !!options.a11y : options.focusable;
        if (focusable && action?.action === 'focus') ctx.keyFocus = key;
        const focused = focusable && ctx.focusable(key, rect, kind);
        const screen = ctx.toScreen(rect);
        ctx.hits.push({ id: key, rect: ctx.clip ? intersect(screen, ctx.clip) : screen, z: ctx.z });
        const input = ctx.input;
        const hovered = ctx.inside(rect) && (ctx.hoverOwner === null || ctx.hoverOwner === key);
        // A touch takes the focus when it lifts as a tap, so a finger that
        // starts a scroll on a widget does not move the focus (or close the
        // keyboard of the field being typed in).
        const touch = input.pointer.type === 'touch';
        if (hovered && input.pointer.pressed) {
            ctx.active = key;
            if (focusable && !touch) ctx.keyFocus = key;
        }
        const held = ctx.active === key && input.pointer.down;
        const clicked = ctx.active === key && hovered && !input.pointer.down && !input.pointer.pressed;
        if (ctx.active === key && !input.pointer.down) {
            ctx.active = null;
            ctx.releasedOn = key;
            if (clicked && focusable && touch) ctx.keyFocus = key;
        }
        const cursor = options.cursor === undefined ? 'pointer' : options.cursor;
        if (cursor && (held || (hovered && ctx.active === null))) ctx.cursor = cursor;
        const keyed = focused && kind !== 'textbox' && ctx.activated(key);
        const anyClick = clicked || keyed || action?.action === 'press';
        // A click usually changes app state the next frame must show (even
        // when nothing animates yet), so ask for that frame.
        if (anyClick) ctx.wake = 0;
        return { hovered, held, clicked: anyClick, id: key, action, focused };
    }
    ctx.interact = interact;

    // ── Motion state ───────────────────────────────────────────────────────

    /** @param {string} key @param {() => any} create */
    function entry(key, create) {
        let item = ctx.motion.get(key);
        if (!item) {
            item = create();
            ctx.motion.set(key, item);
        }
        return item;
    }

    /** @param {Spring} spring */
    function advance(spring) {
        if (ctx.reducedMotion) spring.snap();
        else spring.step(ctx.dt);
        if (!spring.settled) ctx.animating = true;
    }

    /** Animated number at an already-scoped key (widgets use this).
     *  @param {string} key @param {number} target @param {SpringPreset} [preset]
     *  @param {{ initial?: number, precision?: number }} [options] @returns {number} */
    ctx.springAt = (key, target, preset = springs.default, options = {}) => {
        const item = entry('n:' + key, () => ({
            spring: new Spring(options.initial === undefined ? target : options.initial, preset,
                options.precision || 0.001), frame: -1
        }));
        if (item.frame !== ctx.frame) {
            item.frame = ctx.frame;
            item.spring.setPreset(preset);
            item.spring.target = target;
            advance(item.spring);
        }
        return item.spring.value;
    };

    /** Set an animated number with no motion (e.g. while a finger drags it).
     *  @param {string} key @param {number} value */
    ctx.snapAt = (key, value) => {
        const item = ctx.motion.get('n:' + key);
        if (!item) return;
        item.spring.snap(value);
        // The next springAt may move it toward a different target (a transition
        // restarting, a drag released): draw that frame.
        ctx.animating = true;
    };

    /** Animated color at an already-scoped key. @param {string} key @param {Color} target @returns {Color} */
    ctx.colorAt = (key, target) => {
        const item = entry('c:' + key, () => ({ from: target, to: target, t: new Spring(1), frame: -1 }));
        if (item.frame !== ctx.frame) {
            item.frame = ctx.frame;
            if (item.to !== target) {
                item.from = mixColors(item.from, item.to, item.t.value);
                item.to = target;
                item.t.snap(0);
                item.t.target = 1;
            }
            advance(item.t);
        }
        return mixColors(item.from, item.to, item.t.value);
    };

    /** Animated rectangle at an already-scoped key. @param {string} key @param {Rect} target @returns {Rect} */
    ctx.rectAt = (key, target, preset = springs.default) => {
        const options = { precision: 0.05 };
        return {
            x: ctx.springAt(key + '.x', target.x, preset, options),
            y: ctx.springAt(key + '.y', target.y, preset, options),
            width: ctx.springAt(key + '.w', target.width, preset, options),
            height: ctx.springAt(key + '.h', target.height, preset, options),
        };
    };

    /** Animate a number toward `target`; returns its current value. The first
     *  call starts at the target (no motion until it changes).
     *  @param {string} key @param {number} target @param {SpringPreset} [preset] */
    ui.spring = (key, target, preset) => ctx.springAt(ctx.fullId(key), target, preset);
    /** Animate a color toward `target` (OKLab interpolation). @param {string} key @param {Color} target */
    ui.color = (key, target) => ctx.colorAt(ctx.fullId(key), target);
    /** Animate a rectangle toward `target`. @param {string} key @param {Rect} target @param {SpringPreset} [preset] */
    ui.rect = (key, target, preset) => ctx.rectAt(ctx.fullId(key), target, preset);

    /** Enter/exit animation: `draw(t)` runs while the content is visible or
     *  animating out, with t going 0 → 1 on enter and back on exit. Returns
     *  whether anything was drawn.
     *  @param {string} key @param {boolean} visible @param {(t: number) => void} draw
     *  @param {SpringPreset} [preset] @returns {boolean} */
    ui.presence = (key, visible, draw, preset = springs.default) => {
        const scoped = 'presence:' + ctx.fullId(key);
        const t = ctx.springAt(scoped, visible ? 1 : 0, preset, { initial: 0 });
        if (!visible && t <= 0.001) {
            ctx.motion.delete('n:' + scoped);
            return false;
        }
        draw(clamp(t, 0, 1.2));
        return true;
    };

    // ── Layers and overlays ────────────────────────────────────────────────

    /** Frosted glass: blur what is already drawn inside the rounded `rect`.
     *  Returns false when the canvas cannot blur its backdrop.
     *  @param {Rect} rect @param {number} radius @param {number} blur */
    function frost(rect, radius, blur) {
        if (blur <= 0.01 || !canvasHas('saveLayer') || !canvasHas('clipRoundRect')) return false;
        sys.canvas.save();
        sys.canvas.clipRoundRect(rect.x, rect.y, rect.width, rect.height, radius, radius);
        sys.canvas.saveLayer(255, rect.x, rect.y, rect.width, rect.height, blur);
        sys.canvas.restore();
        sys.canvas.restore();
        return true;
    }

    /** Draw `draw(rect)` faded, offset, and scaled around the rectangle's
     *  center. Hit testing follows the transform. Opacity fades the group as
     *  a whole (an offscreen layer; without one, it multiplies the colors the
     *  library draws). `backdrop` blurs what is below the rectangle, growing
     *  with the opacity, for frosted panels that fade in.
     *  @param {Rect} rect @param {LayerOptions} options @param {(rect: Rect) => void} draw */
    ui.layer = (rect, options, draw) => {
        const opacity = clamp(options.opacity === undefined ? 1 : options.opacity, 0, 1);
        if (opacity <= 0.001) return;
        const scale = options.scale === undefined ? 1 : options.scale;
        const dx = options.x || 0, dy = options.y || 0;
        const cx = rect.x + rect.width / 2, cy = rect.y + rect.height / 2;
        const saved = { transform: ctx.transform, alpha: ctx.alpha };
        const { a, tx, ty } = ctx.transform;
        ctx.transform = { a: a * scale, tx: a * (cx * (1 - scale) + dx) + tx, ty: a * (cy * (1 - scale) + dy) + ty };
        const grouped = opacity < 0.999 && canvasHas('saveLayer');
        if (!grouped) ctx.alpha *= opacity;
        sys.canvas.save();
        sys.canvas.translate(cx + dx, cy + dy);
        sys.canvas.scale(scale, scale);
        sys.canvas.translate(-cx, -cy);
        if (options.backdrop)
            frost(rect, options.radius === undefined ? ctx.px(ctx.theme.radius) : options.radius, options.backdrop * opacity);
        if (grouped) sys.canvas.saveLayer(Math.round(opacity * 255));
        try {
            draw(rect);
        } finally {
            if (grouped) sys.canvas.restore();
            sys.canvas.restore();
            ctx.transform = saved.transform;
            ctx.alpha = saved.alpha;
        }
    };

    /** A frosted-glass panel: blurs what is below `rect`, then tints it.
     *  Falls back to a plain surface where the canvas cannot blur.
     *  @param {Rect} rect @param {{ blur?: number, tint?: Color, radius?: number }} [options] */
    ui.glass = (rect, options = {}) => {
        const radius = options.radius === undefined ? ctx.px(ctx.theme.radius) : options.radius;
        const blur = options.blur === undefined ? ctx.px(24) : options.blur;
        const frosted = frost(rect, radius, blur);
        fill(rect, options.tint || (frosted ? withAlpha(ctx.theme.surface, 0.72) : ctx.theme.surface), radius);
    };

    /** Draw `draw()` after the main content, above it, with no clip. Use for
     *  menus, popovers, tooltips, and dialogs. @param {() => void} draw */
    ui.overlay = draw => {
        ctx.overlays.push({ draw, scopes: [...ctx.scopes] });
    };

    /** Prefix the ids created inside `draw` with `id`. @param {string} id @param {() => void} draw */
    ui.scope = (id, draw) => {
        ctx.scopes.push(String(id));
        try {
            draw();
        } finally {
            ctx.scopes.pop();
        }
    };

    // ── Drawing helpers ────────────────────────────────────────────────────

    /** Whether the runtime canvas has `name` (older runtimes and test mocks may not).
     *  @param {string} name */
    function canvasHas(name) {
        return typeof sys.canvas[name] === 'function';
    }
    ctx.canvasHas = canvasHas;

    /** @param {Color} color @returns {Color} */
    ctx.paint = color => (ctx.alpha >= 0.999 ? color : withAlpha(color, ctx.alpha));

    /** @param {string} value @param {number} size @returns {number} */
    /** Text width, cached. `font` names the active font when it is not the default. */
    ctx.measure = (value, size, font = null) => {
        const key = (font === null ? '' : font + '\u0001') + size + '\u0000' + value;
        let width = ctx.measured.get(key);
        if (width === undefined) {
            if (ctx.measured.size > 4000) ctx.measured.clear();
            width = sys.canvas.measureText(value, size);
            ctx.measured.set(key, width);
        }
        return width;
    };

    /** @param {Rect} rect @param {Color} color @param {number} [radius] */
    function fill(rect, color, radius = ctx.px(ctx.theme.radius)) {
        sys.canvas.setFillColor(ctx.paint(color));
        sys.canvas.drawRoundRect(rect.x, rect.y, Math.max(0, rect.width), Math.max(0, rect.height), radius, radius);
    }
    /** Stroke inside `rect`, so a clip at its edge keeps the whole line.
     *  @param {Rect} rect @param {Color} color @param {number} [width] @param {number} [radius] */
    ctx.outline = (rect, color, width = 1, radius = ctx.px(ctx.theme.radius)) => {
        const half = width / 2, corner = Math.max(0, radius - half);
        sys.canvas.setStrokeColor(ctx.paint(color));
        sys.canvas.setStrokeWidth(width);
        sys.canvas.drawRoundRect(rect.x + half, rect.y + half, Math.max(0, rect.width - width),
            Math.max(0, rect.height - width), corner, corner);
    };
    /** @param {number} x1 @param {number} y1 @param {number} x2 @param {number} y2 @param {Color} color @param {number} [width] */
    ctx.line = (x1, y1, x2, y2, color, width = 1) => {
        sys.canvas.setStrokeColor(ctx.paint(color));
        sys.canvas.setStrokeWidth(width);
        sys.canvas.drawLine(x1, y1, x2, y2);
    };
    /** @param {string | number} value @param {number} x @param {number} y @param {number} [size] @param {Color} [color] */
    function text(value, x, y, size = ctx.sp(ctx.theme.font), color = ctx.theme.ink) {
        sys.canvas.setFillColor(ctx.paint(color));
        sys.canvas.drawText(String(value), x, y, size);
    }
    ctx.fill = fill;
    ctx.text = text;

    /** Drop shadow below an elevated surface at `level` (theme.elevation,
     *  1-based). Draw it before the surface. No-op where the canvas cannot blur.
     *  @param {Rect} rect @param {number} [level] @param {number} [radius] @param {number} [opacity] */
    function shadow(rect, level = 1, radius = ctx.px(ctx.theme.radius), opacity = 1) {
        const steps = ctx.theme.elevation;
        if (!steps || !steps.length || opacity <= 0.001 || !canvasHas('setImageFilter')) return;
        const step = steps[clamp(Math.round(level), 1, steps.length) - 1];
        sys.canvas.setImageFilter('blur', ctx.px(step.blur));
        fill({ ...rect, y: rect.y + ctx.px(step.y) }, withAlpha(ctx.theme.shadow, opacity), radius);
        sys.canvas.setImageFilter(null);
    }
    ctx.shadow = shadow;

    /** Fill with the accent: `theme.accentGradient` (top to bottom) when the
     *  theme sets one and the canvas has gradients, else the flat `color`.
     *  Gradient stops animate like other colors, under `key`.
     *  @param {string} key @param {Rect} rect @param {Color} color @param {number} [radius] */
    ctx.fillAccent = (key, rect, color, radius = ctx.px(ctx.theme.radius)) => {
        const stops = ctx.theme.accentGradient;
        if (!stops || stops.length < 2 || !canvasHas('setGradient')) return fill(rect, color, radius);
        const colors = stops.map((stop, index) => ctx.paint(ctx.colorAt(key + '#g' + index, stop)));
        sys.canvas.setFillColor(colors[0]);
        sys.canvas.setGradient('linear', rect.x, rect.y, rect.x, rect.y + rect.height, colors);
        sys.canvas.drawRoundRect(rect.x, rect.y, Math.max(0, rect.width), Math.max(0, rect.height), radius, radius);
        sys.canvas.setGradient(null);
    };

    // ── Frame lifecycle ────────────────────────────────────────────────────

    /** Start a frame. `bounds` defaults to the whole window.
     *  @param {InputState} frameInput @param {Rect} [frameBounds] */
    function begin(frameInput, frameBounds) {
        ctx.input = frameInput;
        ctx.density = typeof sys.window?.getDisplayDensity === 'function' ? sys.window.getDisplayDensity() || 1 : 1;
        ctx.dt = clamp(frameInput.deltaTime || 1 / 60, 0, 0.05);
        ctx.frame++;
        ctx.beginPlatform();
        ctx.beginFocus();
        const system = ctx.system;
        ctx.reducedMotion = ctx.reducedMotionOverride === null ? !!system.reducedMotion : ctx.reducedMotionOverride;
        if (ctx.systemTheme) {
            const wanted = system.darkMode ? themes.dark : themes.light;
            if (ctx.theme.background !== wanted.background) ctx.theme = { ...wanted };
        }
        // Without explicit bounds: the window minus notches and system bars.
        const safe = system.safeArea || { top: 0, right: 0, bottom: 0, left: 0 };
        const width = typeof sys.window?.getWidth === 'function' ? sys.window.getWidth() : 0;
        const height = typeof sys.window?.getHeight === 'function' ? sys.window.getHeight() : 0;
        // The on-screen keyboard covers the bottom: lay out above it, like a
        // window the system resizes. The change glides; a focused field in a
        // scroll area then scrolls into view (see text fields).
        const keyboard = Math.max(0, (system.keyboardInset || 0) - safe.bottom);
        ctx.keyboardOpened = keyboard > (ctx.keyboardInset || 0) + 1;
        ctx.keyboardInset = keyboard;
        const keyboardShown = ctx.springAt('ui:keyboard', keyboard, springs.snappy, { precision: 0.5 });
        const keyboardMoving = !frameBounds && Math.abs(keyboardShown - keyboard) > 0.5;
        // Fields reveal themselves while the keyboard opens and the view shrinks.
        ctx.keyboardChanging = ctx.keyboardOpened || (keyboardMoving && keyboard > 0);
        ctx.bounds = frameBounds || {
            x: safe.left, y: safe.top,
            width: Math.max(0, width - safe.left - safe.right),
            height: Math.max(0, height - safe.top - safe.bottom - keyboardShown),
        };
        // Taps: a release close to where the pointer went down. Widgets that
        // must not react to the start of a scroll (text fields on touch
        // screens) act on taps; releasedOn is the widget a press ended on.
        const pointer = frameInput.pointer;
        if (pointer.pressed) ctx.pressStart = { x: pointer.x, y: pointer.y };
        ctx.pointerReleased = ctx.pointerWasDown && !pointer.down;
        ctx.tapped = ctx.pointerReleased && !!ctx.pressStart
            && Math.hypot(pointer.x - ctx.pressStart.x, pointer.y - ctx.pressStart.y) < ctx.px(10);
        ctx.pointerWasDown = !!pointer.down;
        ctx.releasedOn = null;
        ctx.seen = new Set();
        ctx.lastHits = ctx.hits;
        ctx.hits = [];
        ctx.overlays = [];
        ctx.clip = null;
        ctx.z = 0;
        ctx.scopes = [];
        ctx.transform = { a: 1, tx: 0, ty: 0 };
        ctx.alpha = 1;
        // The bounds still glide toward the keyboard: keep drawing frames.
        ctx.animating = keyboardMoving;
        // Topmost hit under the pointer: highest layer, then latest drawn.
        ctx.hoverOwner = null;
        const { x, y } = frameInput.pointer;
        let best = -1;
        for (const hit of ctx.lastHits)
            if (hit.z >= best && contains(hit.rect, x, y)) {
                best = hit.z;
                ctx.hoverOwner = hit.id;
            }
    }

    function end() {
        // Overlays draw last, each above the previous, unclipped.
        const overlays = ctx.overlays;
        ctx.overlays = [];
        overlays.forEach((overlay, index) => {
            ctx.z = index + 1;
            ctx.clip = null;
            ctx.scopes = overlay.scopes;
            overlay.draw();
        });
        ctx.z = 0;
        ctx.scopes = [];
        // Hover comes from last frame's widgets, so a press made right after
        // the UI changed can miss; give it to this frame's topmost widget.
        if (ctx.input.pointer.pressed && ctx.active === null) {
            const { x, y } = ctx.input.pointer;
            let best = -1;
            for (const hit of ctx.hits)
                if (hit.z >= best && contains(hit.rect, x, y)) {
                    best = hit.z;
                    ctx.active = hit.id;
                }
        }
        ui._endText();
        if (!ctx.input.pointer.down) ctx.active = null;
        ctx.drawToasts();
        ctx.endFocus();
        ctx.endPlatform();
        if (ctx.frame % 30 === 0)
            for (const [key, item] of ctx.motion)
                if (item.frame < ctx.frame - MOTION_TTL_FRAMES) ctx.motion.delete(key);
    }

    /** Clear the canvas with the (animated) theme background. */
    function clear() {
        sys.canvas.clear(ctx.input ? ctx.colorAt('ui:background', ctx.theme.background) : ctx.theme.background);
    }

    /** Switch theme; colors animate to the new values. "system" follows the
     *  system dark mode with themes.light / themes.dark.
     *  @param {Partial<UITheme> | 'system'} next */
    function setTheme(next) {
        ctx.systemTheme = next === 'system';
        if (!ctx.systemTheme) ctx.theme = { ...themes.light, ...next };
        else ctx.theme = { ...(ctx.system.darkMode ? themes.dark : themes.light) };
    }

    // ── Layout (theme-aware defaults) ──────────────────────────────────────

    /** @param {Rect} rect @param {LayoutSize[]} sizes @param {number} [gap] */
    const rows = (rect, sizes, gap = ctx.px(ctx.theme.gap)) => stack(rect, sizes, gap, false);
    /** @param {Rect} rect @param {LayoutSize[]} sizes @param {number} [gap] */
    const columns = (rect, sizes, gap = ctx.px(ctx.theme.gap)) => stack(rect, sizes, gap, true);

    /** Natural width of `value` in the theme font (or `size`), for `{ content }` sizes.
     *  @param {string} value @param {number} [size] */
    const textWidth = (value, size = ctx.sp(ctx.theme.font)) => ctx.measure(String(value), size);
    /** Natural width of a button labeled `label`. @param {string} label */
    const buttonWidth = label => textWidth(label) + ctx.px(ctx.theme.padding) * 2;

    Object.assign(ui, {
        begin, end, clear, setTheme, fill, text, inset, rows, columns, interact, outline: ctx.outline,
        /** @param {Rect} rect @param {number} [level] 1 (raised) to 3 (dialogs) @param {{ radius?: number, opacity?: number }} [options] */
        shadow: (rect, level = 1, options = {}) => shadow(rect, level, options.radius, options.opacity),
        textWidth, buttonWidth,
        /** @param {number} value density-independent pixels @returns {number} physical pixels */
        dp: ctx.px,
    });
    Object.defineProperties(ui, {
        theme: { get: () => ctx.theme },
        colors: { get: () => ctx.theme },
        input: { get: () => ctx.input },
        bounds: { get: () => ctx.bounds },
        clip: { get: () => ctx.clip, set: value => { ctx.clip = value; } },
        focus: { get: () => ctx.focus, set: value => { ctx.focus = value; } },
        animating: { get: () => ctx.animating },
        /** Animations jump to their end. Follows the system setting until the app sets it. */
        reducedMotion: {
            get: () => ctx.reducedMotion,
            set: value => {
                ctx.reducedMotionOverride = value === null ? null : !!value;
                ctx.reducedMotion = value === null ? !!ctx.system.reducedMotion : !!value;
            },
        },
    });

    installPlatform(ui, ctx);
    installFocus(ui, ctx);
    installIcons(ui, ctx);
    installBasic(ui, ctx);
    installContainers(ui, ctx);
    installText(ui, ctx);
    installTextArea(ui, ctx);
    installData(ui, ctx);
    installSelect(ui, ctx);
    installContent(ui, ctx);
    installChoices(ui, ctx);
    installOverlays(ui, ctx);
    installFeedback(ui, ctx);
    installNavigation(ui, ctx);
    return ui;
}
