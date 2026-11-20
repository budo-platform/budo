/** Split panes, scroll areas, and lists. */

import { clamp } from '../math.js';
import { withAlpha } from '../color.js';
import { springs } from '../motion.js';

/** @typedef {import('../layout.js').Rect} Rect */
/** @typedef {{ ratio?: number, min?: number, max?: number, vertical?: boolean }} SplitOptions */
/** @typedef {{ touchScroll?: boolean, wheelScroll?: boolean, followEnd?: boolean, radius?: number, dragThreshold?: number,
 *   scrubber?: Rect, scrubberLabel?: string, scrubberColor?: import('../color.js').Color, accelerate?: boolean,
 *   reset?: boolean, initialOffset?: number, targetOffset?: number,
 *   document?: { offset: number, size: number, visible: number, seek: (offset: number) => void,
 *     edge: (offset: number, delta: number) => void, before: boolean, after: boolean } }} ScrollOptions
 *  `dragThreshold` (dp, default 7): how far a finger moves before a touch scrolls instead of tapping. */
/** @typedef {{ id: string, title: string, subtitle?: string, depth?: number,
 *   kind?: 'group' | 'item', groupId?: string, height?: number,
 *   action?: boolean, actionLabel?: string, actionName?: string, draggable?: boolean,
 *   swipe?: { label: string, danger?: boolean } }} ListItem  `draggable: false` hides the row's reorder handle */
/** @typedef {{ selectedId?: string | null, rowHeight?: number, indent?: number,
 *   emptyText?: string, reorder?: boolean, animate?: boolean, dragThreshold?: number }} ListOptions */
/** @typedef {{ selectedId: string | null, actionId: string | null,
 *   move: { fromId: string, toId: string } | null, swipedId: string | null }} ListResult */

const KEY_DOWN = 81, KEY_UP = 82, KEY_LEFT = 80, KEY_RIGHT = 79, KEY_HOME = 74, KEY_END = 77;

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    const t = () => ctx.theme;
    /** @type {Map<string, number>} */
    const ratios = new Map();
    /** @type {Map<string, number>} target scroll offsets */
    const offsets = new Map();
    /** @type {Map<string, number>} */
    const scrollLimits = new Map();
    /** @type {Map<string, { pointerId: number, lastY: number, startY: number, dragging: boolean, velocity: number }>} */
    const scrollGestures = new Map();
    /** @type {Map<string, { sourceId: string, startY: number, grabY: number, moved: boolean }>} */
    /** @type {Map<string, number>} offsets as drawn last frame */
    const shownOffsets = new Map();
    /** @type {{ id: string, pointerId: number, grab: number } | null} */
    let thumbDrag = null;
    const listDrags = new Map();
    /** @type {Map<string, { itemId: string, startX: number, startY: number, mode: 'swipe' | 'scroll' | null, slide: number }>} */
    const swipes = new Map();

    /** Two panes with a draggable divider; remembers the ratio.
     *  @param {string} id @param {Rect} rect @param {(rect: Rect) => void} drawFirst
     *  @param {(rect: Rect) => void} drawSecond @param {SplitOptions} [options] */
    function split(id, rect, drawFirst, drawSecond, options = {}) {
        const px = ctx.px;
        const vertical = options.vertical === true;
        const length = vertical ? rect.height : rect.width;
        const key = ctx.fullId(id);
        const ratio = ratios.has(key) ? ratios.get(key) : options.ratio || 0.5;
        const bar = length * ratio;
        const half = px(4), gap = px(5);
        const divider = vertical
            ? { x: rect.x, y: rect.y + bar - half, width: rect.width, height: half * 2 }
            : { x: rect.x + bar - half, y: rect.y, width: half * 2, height: rect.height };
        const first = vertical
            ? { x: rect.x, y: rect.y, width: rect.width, height: Math.max(0, bar - gap) }
            : { x: rect.x, y: rect.y, width: Math.max(0, bar - gap), height: rect.height };
        const second = vertical
            ? { x: rect.x, y: rect.y + bar + gap, width: rect.width, height: Math.max(0, length - bar - gap) }
            : { x: rect.x + bar + gap, y: rect.y, width: Math.max(0, length - bar - gap), height: rect.height };
        drawFirst(first);
        drawSecond(second);
        // A generous hit band around the thin visible divider.
        const hit = vertical
            ? { ...divider, y: divider.y - px(14), height: px(36) }
            : { ...divider, x: divider.x - px(14), width: px(36) };
        const state = ctx.interact(id, hit, { cursor: vertical ? 'ns-resize' : 'ew-resize' });
        ctx.fill(divider, ctx.colorAt(state.id + '#bar', state.hovered || state.held ? t().accent : t().border), px(2));
        if (state.held) ratios.set(key, clamp(((vertical ? ctx.input.pointer.y - rect.y : ctx.input.pointer.x - rect.x) /
            Math.max(1, length)), options.min || 0.2, options.max || 0.8));
    }

    /** @param {string} id @param {Rect} lane @param {Rect} hoverArea @param {number} offset
     *  @param {number} viewport @param {number} limit @param {boolean} horizontal @param {string} [label] */
    function scrollbar(id, lane, hoverArea, offset, viewport, limit, horizontal, label) {
        const key = ctx.fullId(id);
        const pointer = ctx.input.pointer;
        const blocked = ctx.modalOpen && ctx.z === 0;
        const length = horizontal ? lane.width : lane.height;
        const visible = limit > 0 && viewport > 0 && length > 0 && (horizontal ? lane.height : lane.width) > 0;
        if (thumbDrag && (ctx.active !== thumbDrag.id || !pointer.down || pointer.id !== thumbDrag.pointerId ||
            (thumbDrag.id === key && (blocked || !visible)))) thumbDrag = null;
        if (!visible) return { offset, dragging: false, draw: () => {} };

        const size = Math.max(Math.min(ctx.px(24), length / 2), length * viewport / (viewport + limit));
        const travel = length - size;
        const position = value => travel * clamp(value / limit, 0, 1);
        const segment = value => horizontal
            ? { ...lane, x: lane.x + position(value), width: size }
            : { ...lane, y: lane.y + position(value), height: size };
        let hovered = false, dragging = false, focused = false;
        if (!blocked) {
            const overThumb = ctx.inside(segment(offset));
            const state = ctx.interact(id, lane, {
                cursor: overThumb || thumbDrag?.id === key ? (horizontal ? 'ew-resize' : 'ns-resize') : null,
                a11y: label ? { role: 'slider', label, min: 0, max: limit, rangeValue: offset,
                    value: Math.round(100 * offset / limit) + '%' } : undefined,
            });
            hovered = state.hovered && overThumb;
            focused = state.focused;
            if (label && !state.held) {
                const action = state.action;
                if (action?.action === 'increment' || (focused && ctx.keyPressed(KEY_RIGHT))) offset += limit / 20;
                if (action?.action === 'decrement' || (focused && ctx.keyPressed(KEY_LEFT))) offset -= limit / 20;
                if (action?.action === 'setValue' && Number.isFinite(Number(action.value))) offset = Number(action.value);
                if (focused && ctx.keyPressed(KEY_HOME)) offset = 0;
                if (focused && ctx.keyPressed(KEY_END)) offset = limit;
                offset = clamp(offset, 0, limit);
            }
            if (state.held && travel > 0) {
                const screen = ctx.toScreen({ ...lane, width: 1, height: 1 });
                const point = horizontal ? (pointer.x - screen.x) / screen.width : (pointer.y - screen.y) / screen.height;
                if (pointer.pressed && (hovered || horizontal)) {
                    thumbDrag = { id: key, pointerId: pointer.id, grab: hovered ? point - position(offset) : size / 2 };
                }
                if (thumbDrag?.id === key) {
                    offset = limit * clamp((point - thumbDrag.grab) / travel, 0, 1);
                    dragging = true;
                }
            }
        }
        return { offset, dragging, draw(value) {
            const thumb = segment(value);
            const thickness = Math.min(ctx.px(4), horizontal ? lane.height : lane.width);
            if (horizontal) {
                thumb.y += lane.height - Math.min(ctx.px(6), lane.height);
                thumb.height = thickness;
            } else {
                thumb.x += lane.width - Math.min(ctx.px(6), lane.width);
                thumb.width = thickness;
            }
            ctx.fill(thumb, ctx.colorAt(key + '#color', dragging || hovered || focused ? t().accent
                : ctx.inside(hoverArea) || ctx.inside(lane) ? t().muted : t().border), thickness / 2);
        } };
    }

    /** Draw a horizontal scrollbar in a reserved gutter and return the new offset.
     *  @param {string} id @param {Rect} gutter @param {number} offset
     *  @param {number} viewportWidth @param {number} contentWidth
     *  @param {{ label?: string, hoverArea?: Rect }} [options] */
    function horizontalScrollbar(id, gutter, offset, viewportWidth, contentWidth, options = {}) {
        const limit = Math.max(0, contentWidth - viewportWidth);
        const bar = scrollbar(id + ':thumb', gutter, options.hoverArea || gutter,
            clamp(offset, 0, limit), viewportWidth, limit, true, options.label || 'Horizontal scroll');
        bar.draw(bar.offset);
        return bar.offset;
    }

    /** Vertical scroll area with a draggable thumb, gliding wheel scrolling,
     *  and touch panning. Clips drawing and hit tests; `draw(content, offset)` gets the
     *  translated content rectangle. Returns the current offset.
     *  @param {string} id @param {Rect} rect @param {number} contentHeight
     *  @param {(content: Rect, offset: number) => void} draw @param {ScrollOptions} [options] */
    function scroll(id, rect, contentHeight, draw, options = {}) {
        const key = ctx.claim(id);
        if (options.reset) {
            const initial = Math.max(0, Math.min(options.initialOffset ?? 0, contentHeight - rect.height));
            offsets.set(key, initial);
            shownOffsets.set(key, initial);
            scrollLimits.delete(key);
            if (!options.document) {
                scrollGestures.delete(key);
                if (thumbDrag?.id === ctx.fullId(id + ':thumb')) thumbDrag = null;
            }
            ctx.snapAt(key + '#offset', initial);
        }
        const input = ctx.input;
        const limit = Math.max(0, contentHeight - rect.height);
        const previousTarget = offsets.get(key) || 0;
        const previousLimit = scrollLimits.get(key) || 0;
        const stretching = !!scrollGestures.get(key)?.dragging; // a finger may hold it past an end
        let target = options.followEnd && previousTarget >= previousLimit - 2 ? limit
            : stretching ? previousTarget : clamp(previousTarget, 0, limit);
        let follow = false;
        const track = options.scrubber;
        const document = options.document;
        // The default thumb can be grabbed anywhere within its reserved gutter.
        const gutter = (limit > 0 || document) && !track ? Math.min(rect.width, ctx.px(12)) : 0;
        const thumbHeight = track ? Math.min(ctx.px(44), track.height / 2) : 0;
        let scrubbing = false;
        let scrubberState = null;
        let overflow = 0;
        if (track && limit < previousLimit && (shownOffsets.get(key) || 0) > limit) {
            target = clamp(target, 0, limit);
            follow = true;
        }
        // An open modal blocks scrolling of the content below it.
        const blocked = ctx.modalOpen && ctx.z === 0;
        if (!blocked && options.wheelScroll !== false && ctx.inside(rect)) {
            const wheel = input.mouse?.wheelY || 0;
            const speed = Math.abs(wheel) / Math.max(ctx.dt, 1 / 240);
            const gain = options.accelerate ? 1 + clamp((speed - 90) / 90, 0, 5) : 1;
            target -= wheel * ctx.px(38) * gain;
            overflow = target - clamp(target, 0, limit);
            target = clamp(target, 0, limit);
        }
        if (!blocked && track && limit > 0) {
            scrubberState = ctx.interact(id + ':scrubber', track, {
                cursor: 'ns-resize',
                a11y: {
                    role: 'slider', label: options.scrubberLabel || 'Scroll position', min: 0, max: limit,
                    rangeValue: clamp(target, 0, limit), value: Math.round(100 * clamp(target, 0, limit) / limit) + '%',
                },
            });
            const previous = target;
            const action = scrubberState.action;
            if (action?.action === 'increment') target += limit / 100;
            if (action?.action === 'decrement') target -= limit / 100;
            if (action?.action === 'setValue' && Number.isFinite(Number(action.value))) target = Number(action.value);
            if (scrubberState.focused) {
                ctx.arrowsTaken = true;
                if (ctx.keyPressed(KEY_DOWN)) target += limit / 100;
                if (ctx.keyPressed(KEY_UP)) target -= limit / 100;
                if (ctx.keyPressed(KEY_HOME)) target = 0;
                if (ctx.keyPressed(KEY_END)) target = limit;
            }
            if (scrubberState.held) {
                const screen = ctx.toScreen({ ...track, height: 1 });
                const pointerY = (input.pointer.y - screen.y) / screen.height;
                target = limit * (pointerY - thumbHeight / 2) / Math.max(Number.EPSILON, track.height - thumbHeight);
            }
            target = clamp(target, 0, limit);
            scrubbing = scrubberState.held || target !== previous;
            if (scrubbing) {
                scrollGestures.delete(key);
                follow = true;
            }
        }
        const documentLimit = document ? Math.max(0, document.size - document.visible) : 0;
        const documentOffset = document ? clamp(document.offset, 0, documentLimit) : 0;
        const bar = scrollbar(id + ':thumb', { ...rect, x: rect.x + rect.width - gutter, width: gutter },
            rect, document ? documentOffset : shownOffsets.get(key) ?? target,
            document ? Math.max(1, document.visible) : rect.height, document ? documentLimit : limit, false);
        if (bar.dragging) {
            if (document) {
                if (Math.round(bar.offset) !== Math.round(documentOffset))
                    document.seek(bar.offset >= documentLimit ? document.size : Math.round(bar.offset));
            } else target = bar.offset;
            scrubbing = follow = true;
        }
        // A parent scroll area must not pan while a nested thumb owns the touch.
        if (!blocked && !scrubbing && !thumbDrag && input.pointer.type === 'touch' && options.touchScroll !== false) {
            if (input.pointer.pressed && ctx.inside(rect) && !(track && ctx.inside(track)))
                scrollGestures.set(key, { pointerId: input.pointer.id, lastY: input.pointer.y, startY: input.pointer.y,
                    dragging: false, velocity: 0 });
            const gesture = scrollGestures.get(key);
            if (gesture && gesture.pointerId === input.pointer.id) {
                if (input.pointer.down) {
                    if (Math.abs(input.pointer.y - gesture.startY) > ctx.px(options.dragThreshold || 7)) gesture.dragging = true;
                    if (gesture.dragging) {
                        // Follow the finger; past either end, stretch with resistance (rubber band).
                        const delta = gesture.lastY - input.pointer.y;
                        const outside = target < 0 || target > limit;
                        const stretch = rect.height * 0.3;
                        const speed = Math.abs(delta) / Math.max(ctx.dt, 1 / 240);
                        const gain = options.accelerate && !outside ? 1 + clamp((speed - ctx.px(300)) / ctx.px(700), 0, 5) : 1;
                        const travel = delta * gain;
                        target = clamp(target + travel * (outside ? 0.4 : 1), -stretch, limit + stretch);
                        if (document && ((target < 0 && document.before) || (target > limit && document.after))) {
                            overflow = target - clamp(target, 0, limit);
                            target = clamp(target, 0, limit);
                        }
                        // Velocity in px/s, smoothed over the last few frames.
                        gesture.velocity = gesture.velocity * 0.6 + (travel / Math.max(ctx.dt, 1 / 240)) * 0.4;
                        ctx.active = null;
                        follow = true;
                    }
                    gesture.lastY = input.pointer.y;
                } else {
                    if (gesture.dragging) {
                        ctx.active = null;
                        // Fling: glide on to where the release speed would carry it; spring back from overscroll.
                        target = target < 0 || target > limit ? clamp(target, 0, limit)
                            : clamp(target + gesture.velocity * 0.35, 0, limit);
                    }
                    scrollGestures.delete(key);
                }
            }
        } else scrollGestures.delete(key);
        // Keyboard focus moved into this area: scroll the widget into view.
        if (ctx.reveal && ctx.reveal.scroll === key) {
            if (!scrubbing || track) {
                const view = ctx.toScreen(rect), margin = ctx.px(12), widget = ctx.reveal.rect;
                const shown = shownOffsets.get(key) ?? target;
                if (widget.y < view.y + margin) target = clamp(shown - (view.y + margin - widget.y), 0, limit);
                else if (widget.y + widget.height > view.y + view.height - margin)
                    target = clamp(shown + widget.y + widget.height - (view.y + view.height - margin), 0, limit);
            }
            ctx.reveal = null;
        }
        if (!blocked && options.targetOffset !== undefined) {
            target = clamp(options.targetOffset, 0, limit);
            overflow = 0;
            follow = true;
        }
        offsets.set(key, target);
        scrollLimits.set(key, limit);
        if (follow) ctx.snapAt(key + '#offset', target);
        // While stretched past an end the offset may leave [0, limit]; the
        // spring brings it back.
        const stretch = rect.height * 0.3;
        const offset = clamp(ctx.springAt(key + '#offset', target, springs.snappy, { precision: 0.25 }), -stretch, limit + stretch);
        shownOffsets.set(key, offset);
        const clip = ctx.pushClip(rect, options.radius === undefined ? ctx.px(t().radius) : options.radius);
        ctx.scrollStack.push(key);
        try {
            draw({ x: rect.x, y: rect.y - offset, width: Math.max(0, rect.width - gutter), height: contentHeight }, offset);
        } finally {
            ctx.scrollStack.pop();
            ctx.popClip(clip);
        }
        if (document && !scrubbing && ((overflow < 0 && document.before) || (overflow > 0 && document.after))) {
            document.edge(target, overflow);
        }
        if (document) bar.draw(bar.offset);
        else if (limit > 0) {
            if (track) {
                const color = options.scrubberColor === undefined ? t().ink : options.scrubberColor;
                const active = scrubberState?.held || scrubberState?.hovered || scrubberState?.focused;
                ctx.fill({
                    x: track.x + track.width / 2 - ctx.px(2), y: track.y + thumbHeight / 2,
                    width: ctx.px(4), height: Math.max(0, track.height - thumbHeight),
                }, withAlpha(color, 0.25), ctx.px(2));
                ctx.fill({
                    x: track.x + track.width / 2 - ctx.px(5),
                    y: track.y + (track.height - thumbHeight) * clamp(offset / limit, 0, 1),
                    width: ctx.px(10), height: thumbHeight,
                }, withAlpha(color, active ? 1 : 0.69), ctx.px(5));
            } else bar.draw(offset);
        }
        return offset;
    }

    /** @param {string} value @param {number} width @param {number} size @returns {string} */
    function truncate(value, width, size) {
        if (width <= 0) return '';
        if (ctx.measure(value, size) <= width) return value;
        let end = value.length;
        while (end > 0 && ctx.measure(value.slice(0, end) + '...', size) > width) end--;
        return end ? value.slice(0, end) + '...' : '';
    }
    ctx.truncate = truncate;

    /** Clipped, variable-height list. The app owns items and selection; rows
     *  glide to new positions, new rows fade in, and with `reorder` the dragged
     *  row follows the pointer while the others make room.
     *  @param {string} id @param {Rect} rect @param {ListItem[]} items
     *  @param {ListOptions} [options] @returns {ListResult} */
    function list(id, rect, items, options = {}) {
        const px = ctx.px;
        const theme = t();
        const input = ctx.input;
        const animate = options.animate !== false;
        const key = ctx.fullId(id);
        const rowHeight = options.rowHeight || px(52);
        const indent = options.indent === undefined ? px(20) : options.indent;
        const heightOf = item => item.height || rowHeight;
        let contentHeight = 0;
        const positions = items.map(item => {
            const top = contentHeight;
            contentHeight += heightOf(item);
            return top;
        });
        /** @type {ListResult} */
        const result = { selectedId: null, actionId: null, move: null, swipedId: null };
        const scrollId = id + ':scroll';
        const scrollKey = ctx.fullId(scrollId);
        const drag = listDrags.get(key);
        const source = drag ? items.find(item => item.id === drag.sourceId) : null;
        const compatible = item => source && item.kind === source.kind && item.groupId === source.groupId;

        /** Index of the compatible row under content-space y, or -1. */
        const targetIndexAt = contentY => positions.findIndex((top, index) =>
            compatible(items[index]) && contentY >= top && contentY < top + heightOf(items[index]));

        // Display order: while dragging an item, the others make room for it.
        let order = items;
        if (drag && drag.moved && source && source.kind !== 'group') {
            const contentY = input.pointer.y - rect.y + (offsets.get(scrollKey) || 0);
            const target = targetIndexAt(contentY);
            if (target >= 0 && items[target].id !== source.id) {
                order = items.filter(item => item !== source);
                order.splice(target, 0, source);
            }
        }
        const tops = new Map();
        let y = 0;
        for (const item of order) {
            tops.set(item.id, y);
            y += heightOf(item);
        }

        if (drag && input.pointer.down && drag.moved) {
            // Auto-scroll near the edges while dragging.
            const edge = Math.min(px(32), rect.height / 4);
            const direction = input.pointer.y < rect.y + edge ? -1 : input.pointer.y > rect.y + rect.height - edge ? 1 : 0;
            if (direction) offsets.set(scrollKey, clamp((offsets.get(scrollKey) || 0) + direction * px(8),
                0, Math.max(0, contentHeight - rect.height)));
        }

        // A press on a row with a swipe action becomes a swipe once it moves
        // sideways, or a scroll once it moves vertically.
        const swipe = swipes.get(key);
        if (swipe) {
            const dx = input.pointer.x - swipe.startX, dy = input.pointer.y - swipe.startY;
            if (input.pointer.down) {
                if (swipe.mode === null && Math.abs(dx) > px(10) && Math.abs(dx) > Math.abs(dy) * 1.5) {
                    swipe.mode = 'swipe';
                    scrollGestures.delete(scrollKey);
                } else if (swipe.mode === null && Math.abs(dy) > px(10)) swipe.mode = 'scroll';
                if (swipe.mode === 'swipe') {
                    ctx.active = null; // no click at the end of a swipe
                    ctx.cursor = 'grabbing';
                }
            } else {
                const row = items.find(item => item.id === swipe.itemId);
                if (swipe.mode === 'swipe' && row && -swipe.slide > Math.min(px(140), rect.width * 0.35)) {
                    result.swipedId = row.id;
                    ui.haptic('medium');
                }
                swipes.delete(key);
            }
        }

        /** @type {(() => void) | null} */
        let drawDragged = null;
        scroll(scrollId, rect, contentHeight, (content) => {
            if (!items.length) ui.label(options.emptyText || 'No items', ui.inset(rect, px(12)), { color: theme.muted });
            for (const item of items) {
                const height = heightOf(item);
                const dragging = !!(drag && drag.moved && source === item);
                const top = animate
                    ? ctx.springAt(key + '#y:' + item.id, tops.get(item.id), springs.default, { precision: 0.05 })
                    : tops.get(item.id);
                let rowY = content.y + top;
                if (dragging) {
                    rowY = input.pointer.y - drag.grabY;
                    ctx.snapAt(key + '#y:' + item.id, rowY - content.y);
                }
                const appear = animate ? ctx.springAt(key + '#in:' + item.id, 1, springs.gentle, { initial: 0 }) : 1;
                const drawRow = () => {
                    if (rowY + height <= rect.y - height || rowY >= rect.y + rect.height + height) return;
                    const depth = Math.max(0, item.depth || 0);
                    const row = {
                        x: content.x + depth * indent, y: rowY + px(2),
                        width: Math.max(0, content.width - depth * indent), height: Math.max(0, height - px(4))
                    };
                    const actionWidth = item.action ? px(38) : 0;
                    const handleWidth = options.reorder && item.draggable !== false ? px(32) : 0;
                    const body = { ...row, width: Math.max(0, row.width - actionWidth - handleWidth) };
                    // Swipe: the row slides with the pointer over its action.
                    const swipe = swipes.get(key);
                    const swiping = !!(swipe && swipe.itemId === item.id && swipe.mode === 'swipe');
                    let slide = 0;
                    if (item.swipe) {
                        if (swiping) {
                            const dx = Math.min(0, input.pointer.x - swipe.startX);
                            const limit = row.width * 0.6;
                            slide = dx < -limit ? -limit + (dx + limit) * 0.3 : dx;
                            ctx.snapAt(key + '#swipe:' + item.id, slide);
                            swipe.slide = slide;
                        }
                        slide = swiping ? slide : ctx.springAt(key + '#swipe:' + item.id, 0, springs.snappy);
                        if (input.pointer.pressed && !drag && ctx.inside(body))
                            swipes.set(key, { itemId: item.id, startX: input.pointer.x, startY: input.pointer.y, mode: null, slide: 0 });
                    }
                    if (slide < -0.5) {
                        const reveal = item.swipe.danger ? theme.danger : theme.accent;
                        ctx.fill(row, reveal);
                        const label = item.swipe.label, labelSize = ctx.sp(theme.small || theme.font);
                        ctx.text(label, row.x + row.width - ctx.measure(label, labelSize) - px(18),
                            row.y + row.height / 2 + labelSize * 0.35, labelSize, theme.accentInk);
                    }
                    const drawContent = () => {
                    if (slide < -0.5) ctx.fill(row, theme.surface);
                        const state = ctx.interact(id + ':row:' + item.id, body, {
                            a11y: {
                                role: item.kind === 'group' ? 'heading' : 'listitem',
                                label: item.subtitle ? item.title + ', ' + item.subtitle : item.title,
                                selected: item.id === options.selectedId,
                            },
                        });
                        const selected = item.id === options.selectedId;
                        if (dragging) {
                            ctx.shadow(row, 2);
                            ctx.fill(row, theme.surface);
                            ctx.outline(row, theme.accent, px(1.5));
                        }
                        const background = selected ? theme.raised : state.hovered ? theme.background : withAlpha(theme.background, 0);
                        ctx.fill(row, ctx.colorAt(state.id + '#bg', background));
                        const size = item.kind === 'group' ? ctx.sp(theme.font) : Math.max(ctx.sp(15), ctx.sp(theme.font) - px(2));
                        const titleWidth = Math.max(0, body.width - px(24));
                        ctx.text(truncate(item.title, titleWidth, size), body.x + px(12),
                            row.y + (item.subtitle ? row.height * 0.48 : row.height / 2 + size * 0.35),
                            size, ctx.colorAt(state.id + '#ink', selected ? theme.accent : theme.ink));
                        if (item.subtitle) ctx.text(truncate(item.subtitle, titleWidth, px(15)), body.x + px(12),
                            row.y + row.height - px(9), px(15), theme.muted);
                        if (state.clicked) result.selectedId = item.id;
                        if (item.action) {
                            const action = { x: body.x + body.width, y: row.y, width: actionWidth, height: row.height };
                            const actionState = ctx.interact(id + ':action:' + item.id, action, {
                                a11y: { role: 'button', label: item.actionName || (item.actionLabel || '...') + ' ' + item.title },
                            });
                            if (actionState.clicked) result.actionId = item.id;
                            const actionLabel = item.actionLabel || '...';
                            const size2 = ctx.sp(theme.font);
                            ctx.text(actionLabel, action.x + (actionWidth - ctx.measure(actionLabel, size2)) / 2,
                                action.y + action.height / 2 + px(5), size2,
                                ctx.colorAt(actionState.id + '#ink', actionState.hovered ? theme.accent : theme.muted));
                        }
                        if (handleWidth > 0) {
                            const handle = { x: row.x + row.width - handleWidth, y: row.y, width: handleWidth, height: row.height };
                            const handleState = ctx.interact(id + ':drag:' + item.id, handle, { cursor: 'grab' });
                            if (input.pointer.pressed && handleState.hovered) {
                                ui.haptic('light');
                                listDrags.set(key, { sourceId: item.id, startY: input.pointer.y, grabY: input.pointer.y - rowY, moved: false });
                                scrollGestures.delete(scrollKey);
                            }
                            ctx.text('::', handle.x + px(7), handle.y + handle.height / 2 + px(5), ctx.sp(theme.font),
                                handleState.hovered || dragging ? theme.accent : theme.muted);
                        }
                    };
                    if (slide < -0.5) ui.layer(row, { x: slide }, drawContent);
                    else drawContent();
                };
                if (dragging) drawDragged = drawRow; // drawn last, above the other rows
                else if (appear < 0.999) ui.layer({ x: content.x, y: rowY, width: content.width, height },
                    { opacity: appear, x: -px(12) * (1 - appear) }, () => drawRow());
                else drawRow();
            }
            if (drawDragged) drawDragged();
        }, { touchScroll: !drag, dragThreshold: options.dragThreshold });

        const currentDrag = listDrags.get(key);
        if (currentDrag) {
            if (input.pointer.down) {
                if (Math.abs(input.pointer.y - currentDrag.startY) > px(7)) currentDrag.moved = true;
                if (currentDrag.moved) ctx.cursor = 'grabbing';
            } else {
                const dropped = items.find(item => item.id === currentDrag.sourceId);
                if (currentDrag.moved && dropped && input.pointer.y >= rect.y && input.pointer.y < rect.y + rect.height) {
                    const contentY = input.pointer.y - rect.y + (offsets.get(scrollKey) || 0);
                    const target = items[positions.findIndex((top, index) =>
                        contentY >= top && contentY < top + heightOf(items[index]))];
                    if (target && target.id !== dropped.id && target.kind === dropped.kind && target.groupId === dropped.groupId)
                    {
                        result.move = { fromId: dropped.id, toId: target.id };
                        ui.haptic('medium');
                    }
                }
                listDrags.delete(key);
            }
        }
        return result;
    }

    Object.assign(ui, { split, scroll, horizontalScrollbar, list });
}
