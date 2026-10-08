// Button icons: SVG path data on a 24-unit grid, drawn as round strokes in
// any color (the same convention as budo-ui's icons).

const ICONS = {
    rotateLeft: 'M4.5 12a7.5 7.5 0 1 0 2.2-5.3M4.5 4v4.5H9',
    rotateRight: 'M19.5 12a7.5 7.5 0 1 1-2.2-5.3M19.5 4v4.5H15',
    chevronUp: 'M6 15l6-6 6 6',
    chevronDown: 'M6 9l6 6 6-6',
    zoomIn: 'M17 10.5a6.5 6.5 0 1 1-13 0a6.5 6.5 0 1 1 13 0zM15.3 15.3L20.5 20.5M10.5 7.5v6M7.5 10.5h6',
    zoomOut: 'M17 10.5a6.5 6.5 0 1 1-13 0a6.5 6.5 0 1 1 13 0zM15.3 15.3L20.5 20.5M7.5 10.5h6',
    target: 'M18.5 12a6.5 6.5 0 1 1-13 0a6.5 6.5 0 1 1 13 0zM12 2.5v4M12 17.5v4M2.5 12h4M17.5 12h4' +
        'M13 12a1 1 0 1 1-2 0a1 1 0 1 1 2 0z',
    viewOrbit: 'M2.5 12c0-2.8 4.3-5 9.5-5s9.5 2.2 9.5 5-4.3 5-9.5 5-9.5-2.2-9.5-5z' +
        'M14.5 12a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0zM19 8.5l2 .8-.6 2',
    viewFollow: 'M3 7.5h11v9H3zM14 10.5l6.5-3.5v10L14 13.5',
    viewFirstPerson: 'M2.5 12s3.5-6 9.5-6 9.5 6 9.5 6-3.5 6-9.5 6-9.5-6-9.5-6z' +
        'M15 12a3 3 0 1 1-6 0a3 3 0 1 1 6 0z',
    sound: 'M4 9.5h3.5L12 5.5v13l-4.5-4H4zM15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11',
    muted: 'M4 9.5h3.5L12 5.5v13l-4.5-4H4zM16 9.5l5 5M21 9.5l-5 5',
    sparkles: 'M10 3.5l1.6 4.9 4.9 1.6-4.9 1.6L10 16.5l-1.6-4.9L3.5 10l4.9-1.6z' +
        'M18 14l.9 2.6 2.6.9-2.6.9-.9 2.6-.9-2.6-2.6-.9 2.6-.9z',
    sparklesOff: 'M10 3.5l1.6 4.9 4.9 1.6-4.9 1.6L10 16.5l-1.6-4.9L3.5 10l4.9-1.6zM3 21L21 3',
    camera: 'M3 8h4l2-3h6l2 3h4v11H3zM15.5 13a3.5 3.5 0 1 1-7 0a3.5 3.5 0 1 1 7 0z',
    sliders: 'M4 6h16M4 12h16M4 18h16M9 3.5v5M15 9.5v5M7 15.5v5',
    plus: 'M12 5v14M5 12h14',
    minus: 'M5 12h14',
    reset: 'M20 12a8 8 0 1 1-2.3-5.6M20 4v4.5h-4.5',
};

const paths = new Map(); // icon name -> path id, -1 when it does not parse

function pathFor(name) {
    let id = paths.get(name);
    if (id === undefined) {
        id = sys.path.create();
        if (id < 0 || !sys.path.addSvg(id, ICONS[name])) id = -1;
        paths.set(name, id);
    }
    return id;
}

// Draw icon `name` centered on (cx, cy), `size` pixels wide.
export function drawIcon(name, cx, cy, size, color, strokeWidth = 2) {
    const id = pathFor(name);
    if (id < 0) return;
    const canvas = sys.canvas;
    const scale = size / 24;
    canvas.save();
    canvas.translate(cx - size / 2, cy - size / 2);
    canvas.scale(scale, scale);
    canvas.setStrokeColor(color);
    canvas.setStrokeWidth(strokeWidth);
    canvas.setStrokeCap('round');
    canvas.setStrokeJoin('round');
    canvas.drawPath(id);
    canvas.setStrokeCap('butt');
    canvas.setStrokeJoin('miter');
    canvas.restore();
}
