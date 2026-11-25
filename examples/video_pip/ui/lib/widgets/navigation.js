/** Navigation: a stack of pages with push/pop transitions, an interactive
 *  back gesture from the left edge, and shared elements that morph from one
 *  page to the next. The app owns the stack (an array of route strings). */

import { clamp, lerp } from '../math.js';
import { springs } from '../motion.js';
import { withAlpha } from '../color.js';

/** @typedef {import('../layout.js').Rect} Rect */

const KEY_ESCAPE = 41;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    /** @type {Map<string, { current: string, previous: string | null, depth: number, direction: number, linear?: boolean }>} */
    const navigators = new Map();
    /** @type {Map<string, { startX: number, active: boolean }>} edge back gestures */
    const edges = new Map();
    /** Shared elements: resting screen rectangles by route and tag, this frame and last. */
    let sharedNow = new Map();
    let sharedLast = new Map();
    /** @type {{ from: string, to: string, p: number, key: string } | null} transition being drawn */
    let transition = null;
    /** @type {string | null} route of the page being drawn */
    let pageRoute = null;
    let pageShift = 0;
    let sharedFrame = -1;

    const ease = p => 1 - Math.pow(1 - clamp(p, 0, 1), 3);

    /** Draw the top page of `stack` in `rect`; animate when the top changes.
     *  Returns true when the user asks to go back (Escape, or a drag from the
     *  left edge); the app then pops its stack.
     *  @param {string} id @param {Rect} rect @param {string[]} stack
     *  @param {(route: string, rect: Rect) => void} drawPage @returns {boolean} */
    function navigator(id, rect, stack, drawPage) {
        const key = ctx.fullId(id);
        if (sharedFrame !== ctx.frame) {
            sharedLast = sharedNow;
            sharedNow = new Map();
            sharedFrame = ctx.frame;
        }
        const top = stack[stack.length - 1];
        let state = navigators.get(key);
        if (!state) {
            state = { current: top, previous: null, depth: stack.length, direction: 1 };
            navigators.set(key, state);
        }
        const input = ctx.input, px = ctx.px;
        let back = false;

        // Interactive back: drag from the left edge to preview the previous page.
        const edge = edges.get(key);
        if (stack.length > 1 && input.pointer.pressed && input.pointer.x < rect.x + px(24) &&
            input.pointer.y >= rect.y && input.pointer.y < rect.y + rect.height)
            edges.set(key, { startX: input.pointer.x, active: false });
        let dragged = null;
        if (edge) {
            const dx = input.pointer.x - edge.startX;
            if (input.pointer.down) {
                if (dx > px(12)) edge.active = true;
                if (edge.active) {
                    dragged = clamp(dx / rect.width, 0, 1);
                    ctx.active = null;
                    ctx.cursor = 'grabbing';
                }
            } else {
                if (edge.active && (dx > rect.width * 0.35 || (input.pointer.dx || 0) > px(20))) back = true;
                edges.delete(key);
            }
        }
        if (stack.length > 1 && ctx.keyPressed(KEY_ESCAPE) && !ctx.modalOpen) back = true;

        if (top !== state.current) {
            const pushed = stack.length >= state.depth;
            state.previous = state.current;
            state.current = top;
            state.direction = pushed ? 1 : -1;
            state.depth = stack.length;
            // A pop finishing a back drag continues from where the finger left it.
            const from = !pushed ? (ctx.motion.get('n:' + key + '#back')?.spring.value || 0) : 0;
            state.linear = from > 0.001;
            ctx.springAt(key + '#p', 1, springs.default, { initial: from });
            ctx.snapAt(key + '#p', from);
            ctx.springAt(key + '#back', 0, springs.snappy);
            ctx.snapAt(key + '#back', 0);
        }
        const p = ctx.springAt(key + '#p', 1, springs.default, { initial: 1 });
        // The preview follows the finger, then springs back if the user lets go early.
        let backPreview = ctx.springAt(key + '#back', 0, springs.snappy);
        if (dragged !== null) {
            ctx.snapAt(key + '#back', dragged);
            backPreview = dragged;
        }

        /** Draw one page, slid by `shift`, faded by `dim`, optionally inert. */
        const page = (route, shift, dim, inert) => {
            const savedRoute = pageRoute, savedShift = pageShift, savedInert = ctx.inert;
            pageRoute = key + '|' + route;
            pageShift = shift;
            ctx.inert = savedInert || inert;
            const clip = ctx.pushClip(rect, 0);
            try {
                ui.layer(rect, { x: shift }, () => {
                    ui.scope(route, () => drawPage(route, rect));
                    if (dim > 0.001) ctx.fill(rect, withAlpha('#000000', 0.25 * dim), 0);
                });
            } finally {
                ctx.popClip(clip);
                pageRoute = savedRoute;
                pageShift = savedShift;
                ctx.inert = savedInert;
            }
        };

        const width = rect.width;
        if (backPreview > 0.001 && stack.length > 1) {
            // Back gesture preview: the previous page parallaxes in from the left.
            const below = stack[stack.length - 2];
            transition = { from: top, to: below, p: backPreview, key };
            page(below, -width * 0.3 * (1 - backPreview), 1 - backPreview, true);
            page(top, width * backPreview, 0, dragged !== null);
            if (dragged === null) ctx.animating = true;
        } else if (p < 0.999 && state.previous !== null) {
            const e = state.linear ? p : ease(p);
            transition = { from: state.previous, to: top, p: e, key };
            if (state.direction > 0) {
                page(state.previous, -width * 0.3 * e, e, true);  // pushed: the old page moves aside and dims
                page(top, width * (1 - e), 0, false);
            } else {
                page(top, -width * 0.3 * (1 - e), 1 - e, false);  // popped: the old page leaves to the right
                page(state.previous, width * e, 0, true);
            }
        } else {
            transition = null;
            page(top, 0, 0, false);
        }
        transition = null;
        return back;
    }

    /** An element that morphs between pages: when the leaving and the entering
     *  page both draw `tag`, it flies from its old rectangle to its new one
     *  during the transition. Call inside a navigator page.
     *  @param {string} tag @param {Rect} rect @param {(rect: Rect) => void} draw */
    function shared(tag, rect, draw) {
        const screen = ctx.toScreen(rect);
        const resting = { ...screen, x: screen.x - pageShift };
        if (pageRoute) sharedNow.set(pageRoute + '#' + tag, resting);
        if (!transition || !pageRoute) return draw(rect);
        const [navKey] = pageRoute.split('|');
        const fromKey = navKey + '|' + transition.from + '#' + tag;
        const toKey = navKey + '|' + transition.to + '#' + tag;
        const own = pageRoute === navKey + '|' + transition.to ? 'to' : 'from';
        const other = sharedLast.get(own === 'to' ? fromKey : toKey) || sharedNow.get(own === 'to' ? fromKey : toKey);
        if (!other) return draw(rect);
        // The leaving page hides its copy; the entering page draws the morph above everything.
        if (own === 'from') return;
        const p = transition.p;
        const mixed = {
            x: lerp(other.x, resting.x, p), y: lerp(other.y, resting.y, p),
            width: lerp(other.width, resting.width, p), height: lerp(other.height, resting.height, p),
        };
        const inert = ctx.inert, scopes = [...ctx.scopes];
        ui.overlay(() => {
            const saved = ctx.inert;
            ctx.inert = inert;
            ctx.scopes = scopes;
            try {
                draw(mixed);
            } finally {
                ctx.inert = saved;
            }
        });
    }

    Object.assign(ui, { navigator, shared });
}
