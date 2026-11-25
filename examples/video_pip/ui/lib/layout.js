/** Rectangle layout. Rectangles are `{x, y, width, height}` in physical
 *  canvas pixels; sizes are numbers (fixed), `{content: n}` (a measured size,
 *  e.g. from ui.textWidth: shrinks proportionally when space runs out), or
 *  `{weight: n}` (a share of the remaining space). */

/** @typedef {{ x: number, y: number, width: number, height: number }} Rect */
/** @typedef {number | { weight?: number } | { content: number }} LayoutSize */

/** @param {Rect} rect @param {number} padding @returns {Rect} */
export function inset(rect, padding) {
    return {
        x: rect.x + padding, y: rect.y + padding,
        width: Math.max(0, rect.width - padding * 2),
        height: Math.max(0, rect.height - padding * 2)
    };
}

/** @param {Rect} rect @param {LayoutSize[]} sizes @param {number} gap
 *  @param {boolean} horizontal @returns {Rect[]} */
export function stack(rect, sizes, gap, horizontal) {
    const length = horizontal ? rect.width : rect.height;
    const isContent = size => typeof size === 'object' && size !== null && 'content' in size;
    const fixed = sizes.reduce((sum, size) => sum + (typeof size === 'number' ? size : 0), 0);
    const content = sizes.reduce((sum, size) => sum + (isContent(size) ? Math.max(0, size.content) : 0), 0);
    const available = Math.max(0, length - gap * Math.max(0, sizes.length - 1) - fixed);
    // Content sizes get what they measure, shrinking together when it does not fit.
    const shrink = content > available ? available / content : 1;
    const remaining = Math.max(0, available - content * shrink);
    const weight = sizes.reduce((sum, size) => sum + (typeof size === 'number' || isContent(size) ? 0 : size.weight || 1), 0);
    let position = horizontal ? rect.x : rect.y;
    return sizes.map(size => {
        const extent = typeof size === 'number' ? size
            : isContent(size) ? Math.max(0, size.content) * shrink
                : remaining * (size.weight || 1) / weight;
        const child = horizontal
            ? { x: position, y: rect.y, width: Math.max(0, extent), height: rect.height }
            : { x: rect.x, y: position, width: rect.width, height: Math.max(0, extent) };
        position += extent + gap;
        return child;
    });
}

/** @param {Rect} first @param {Rect} second @returns {Rect} */
export function intersect(first, second) {
    const x = Math.max(first.x, second.x);
    const y = Math.max(first.y, second.y);
    return {
        x, y, width: Math.max(0, Math.min(first.x + first.width, second.x + second.width) - x),
        height: Math.max(0, Math.min(first.y + first.height, second.y + second.height) - y)
    };
}

/** @param {Rect} rect @param {number} x @param {number} y @returns {boolean} */
export function contains(rect, x, y) {
    return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
        x < rect.x + rect.width && y < rect.y + rect.height;
}

/** Scale a rectangle around its center. @param {Rect} rect @param {number} factor @returns {Rect} */
export function scaleRect(rect, factor) {
    const width = rect.width * factor, height = rect.height * factor;
    return { x: rect.x + (rect.width - width) / 2, y: rect.y + (rect.height - height) / 2, width, height };
}
