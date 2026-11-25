/** Icons: SVG path data on a 24-unit grid, drawn as strokes in any color
 *  (so they follow the theme and animate with it). `ui.icon(name, rect)`
 *  draws a built-in or registered icon, or raw path data. */

/** @typedef {import('./layout.js').Rect} Rect */
/** @typedef {{ d: string, fill?: boolean }} IconDefinition */

/** Built-in icons: 24×24 grid, 2-unit round strokes. */
export const ICONS = {
    check: 'M5 12.5l4.5 4.5L19 7.5',
    close: 'M6 6l12 12M18 6L6 18',
    plus: 'M12 5v14M5 12h14',
    minus: 'M5 12h14',
    chevronLeft: 'M15 6l-6 6 6 6',
    chevronRight: 'M9 6l6 6-6 6',
    chevronUp: 'M6 15l6-6 6 6',
    chevronDown: 'M6 9l6 6 6-6',
    arrowLeft: 'M19 12H5M11 6l-6 6 6 6',
    arrowRight: 'M5 12h14M13 6l6 6-6 6',
    menu: 'M4 7h16M4 12h16M4 17h16',
    more: 'M4 12a1.5 1.5 0 1 0 3 0a1.5 1.5 0 1 0-3 0M10.5 12a1.5 1.5 0 1 0 3 0a1.5 1.5 0 1 0-3 0' +
        'M17 12a1.5 1.5 0 1 0 3 0a1.5 1.5 0 1 0-3 0',
    search: 'M17.5 11a6.5 6.5 0 1 1-13 0a6.5 6.5 0 1 1 13 0zM16 16l4.5 4.5',
    trash: 'M4 7h16M10 3.5h4M6 7l1 13h10l1-13M10 11v5.5M14 11v5.5',
    edit: 'M4 20h4L19 9l-4-4L4 16v4zM13 7l4 4',
    copy: 'M9 9h11v11H9zM5 15H4V4h11v1',
    star: 'M12 3l2.8 5.9 6.4.7-4.8 4.4 1.4 6.4L12 17.1l-5.8 3.3 1.4-6.4-4.8-4.4 6.4-.7z',
    heart: 'M12 20s-7.5-4.6-7.5-10.2A4.3 4.3 0 0 1 12 7a4.3 4.3 0 0 1 7.5 2.8C19.5 15.4 12 20 12 20z',
    home: 'M4 11l8-7 8 7M6 9.5V20h4v-6h4v6h4V9.5',
    user: 'M16 8a4 4 0 1 1-8 0a4 4 0 1 1 8 0zM4.5 20a7.5 7.5 0 0 1 15 0',
    info: 'M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0zM12 11v5M12 7.5v.5',
    alert: 'M12 4L2.5 20h19L12 4zM12 10v4M12 17v.5',
    refresh: 'M20 12a8 8 0 1 1-2.3-5.6M20 4v4.5h-4.5',
    sun: 'M15.5 12a3.5 3.5 0 1 1-7 0a3.5 3.5 0 1 1 7 0zM12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2' +
        'M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4',
    moon: 'M20 14.5A8 8 0 1 1 9.5 4a7.5 7.5 0 0 0 10.5 10.5z',
    folder: 'M3 6.5A1.5 1.5 0 0 1 4.5 5h4l2 2.5h9A1.5 1.5 0 0 1 21 9v9.5a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 18.5z',
    file: 'M6 3h8l4 4v14H6zM14 3v4h4',
};

/** @param {any} ui @param {any} ctx */
export function install(ui, ctx) {
    /** @type {Map<string, IconDefinition>} */
    const registry = new Map(Object.entries(ICONS).map(([name, d]) => [name, { d }]));
    /** @type {Map<string, number>} path ids by path data (-1: does not parse) */
    const paths = new Map();
    const supported = () => typeof sys.path?.addSvg === 'function' && typeof sys.canvas.drawPath === 'function';

    /** The path id of `d`, built once. */
    function pathFor(d) {
        let id = paths.get(d);
        if (id === undefined) {
            id = sys.path.create();
            if (id < 0 || !sys.path.addSvg(id, d)) id = -1;
            paths.set(d, id);
        }
        return id;
    }

    /** Add an icon to `ui.icon` by name. `d` is SVG path data on a 24-unit
     *  grid; strokes by default, or `{ fill: true }` for solid shapes.
     *  @param {string} name @param {string} d @param {{ fill?: boolean }} [options] */
    function registerIcon(name, d, options = {}) {
        registry.set(name, { d, fill: !!options.fill });
    }

    /** Draw an icon centered in `rect`, as large as its shorter side.
     *  `icon` is a name (built-in or registered) or raw path data.
     *  @param {string} icon @param {Rect} rect
     *  @param {{ color?: string, size?: number, strokeWidth?: number, fill?: boolean }} [options] */
    function icon(icon, rect, options = {}) {
        if (!supported()) return;
        const entry = registry.get(icon) || { d: icon, fill: options.fill };
        const id = pathFor(entry.d);
        if (id < 0) return;
        const size = options.size || Math.min(rect.width, rect.height);
        const scale = size / 24;
        const color = ctx.paint(options.color || ctx.theme.ink);
        const canvas = sys.canvas;
        canvas.save();
        canvas.translate(rect.x + (rect.width - size) / 2, rect.y + (rect.height - size) / 2);
        canvas.scale(scale, scale);
        if (entry.fill || options.fill) canvas.setFillColor(color);
        else {
            canvas.setStrokeColor(color);
            canvas.setStrokeWidth(options.strokeWidth || 2);
            canvas.setStrokeCap?.('round');
            canvas.setStrokeJoin?.('round');
        }
        canvas.drawPath(id);
        if (!entry.fill && !options.fill) {
            canvas.setStrokeCap?.('butt');
            canvas.setStrokeJoin?.('miter');
        }
        canvas.restore();
    }

    ctx.icon = icon;
    Object.assign(ui, { icon, registerIcon });
}
