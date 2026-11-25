const TAU = Math.PI * 2;
const CYAN = [0.20, 0.88, 1, 0.96];
const BLUE = [0.12, 0.48, 1, 0.86];
const VIOLET = [0.65, 0.34, 1, 0.91];
const PINK = [0.95, 0.49, 0.96, 0.94];
const WHITE = [0.64, 0.96, 1, 1];
const GRID = [0.20, 0.55, 1, 0.27];
const FAINT = [0.15, 0.38, 0.90, 0.14];
const MAX_VERTICES = 90000;
const PARTICLES = 520;
const MOTES = 120;
export const METRIC_X = [-5.02, -3.96, -2.90, -1.84];
export const LIFT = 0.62;
const SERIES_COLORS = [CYAN, BLUE, VIOLET, PINK];
const PARTICLE_COLORS = [CYAN, WHITE, VIOLET, PINK, BLUE];

// Board footprints in world space. Upright widgets (pivot/rise/upright) fold flat when focused.
export const PANELS = [
    { name: 'Key Metrics', x: -5.91, y: 0.23, z: 1.94, w: 4.98, l: 1.89 },
    { name: 'Analytics Chart', x: 1.34, y: 0.23, z: -3.50, w: 4.68, l: 0.64, pivot: 0.30, rise: 2.25, upright: -3.22 },
    { name: 'Recent Activity', x: 2.99, y: 0.22, z: -1.06, w: 3.34, l: 2.60 },
    { name: 'Tasks', x: -5.68, y: 0.22, z: -1.06, w: 3.30, l: 2.60 },
    { name: 'Performance', x: -5.40, y: 0.19, z: -3.58, w: 4.37, l: 0.93, pivot: 0.24, rise: 2.05, upright: -3.64 },
    { name: 'Regional breakdown', x: 1.80, y: 0.25, z: 1.84, w: 4.14, l: 1.96 },
    { name: 'Globe', x: -2.45, y: 0.32, z: -2.45, w: 4.90, l: 4.90, globe: true },
];

export const smooth = t => {
    const x = Math.min(1, Math.max(0, t));
    return x * x * (3 - 2 * x);
};

export function placePoint(panel, amount, x, y, z, out) {
    const a = smooth(amount);
    if (panel.pivot !== undefined && y > panel.pivot && a > 0) {
        const dy = y - panel.pivot;
        const fold = a * Math.PI * 0.5;
        y = panel.pivot + dy * Math.cos(fold);
        z -= dy * Math.sin(fold);
    }
    if (!panel.globe) y += LIFT * a;
    out[0] = x; out[1] = y; out[2] = z;
    return out;
}

export function panelBounds(panel) {
    const top = panel.pivot === undefined ? panel.z : Math.min(panel.z, panel.upright - panel.rise);
    return { x0: panel.x, x1: panel.x + panel.w, z0: top, z1: panel.z + panel.l,
        y: panel.globe ? panel.y : panel.y + LIFT };
}

// Points whose screen bounds define the clickable area of a panel.
export function panelHull(panel) {
    const { x, y, z, w, l } = panel;
    if (panel.globe) {
        const points = [];
        for (const px of [-1.35, 1.35]) for (const py of [0.30, 3.35]) for (const pz of [-1.35, 1.35]) points.push([px, py, pz]);
        return points;
    }
    const points = [[x, y, z], [x + w, y, z], [x, y, z + l], [x + w, y, z + l]];
    if (panel.pivot !== undefined) {
        points.push([x, panel.pivot + panel.rise, panel.upright], [x + w, panel.pivot + panel.rise, panel.upright]);
    }
    return points;
}

function hash(n) {
    const s = Math.sin(n * 127.1 + 311.7) * 43758.5453;
    return s - Math.floor(s);
}

export function createHolograms(gl, globeHeight, globeRadius) {
    const data = new Float32Array(MAX_VERTICES * 7);
    const buffer = gl.createBuffer('vertex', data, 'stream');
    const layout = gl.createVertexLayout();
    gl.setAttribute(layout, 0, buffer, 3, 'float', false, 28, 0);
    gl.setAttribute(layout, 3, buffer, 4, 'float', false, 28, 12);

    const sphere = [];
    function sphereVertex(row, column) {
        const latitude = Math.PI * (row / 32 - 0.5);
        const longitude = TAU * column / 64;
        const nx = Math.cos(latitude) * Math.sin(longitude);
        const ny = Math.sin(latitude);
        const nz = Math.cos(latitude) * Math.cos(longitude);
        const r = globeRadius;
        sphere.push(r * nx, globeHeight + r * ny, r * nz, nx, ny, nz);
    }
    for (let j = 0; j < 32; j++) {
        for (let i = 0; i < 64; i++) {
            sphereVertex(j, i); sphereVertex(j, i + 1); sphereVertex(j + 1, i + 1);
            sphereVertex(j, i); sphereVertex(j + 1, i + 1); sphereVertex(j + 1, i);
        }
    }
    const globeBuffer = gl.createBuffer('vertex', new Float32Array(sphere));
    const globeLayout = gl.createVertexLayout();
    gl.setAttribute(globeLayout, 0, globeBuffer, 3, 'float', false, 24, 0);
    gl.setAttribute(globeLayout, 2, globeBuffer, 3, 'float', false, 24, 12);

    const particles = new Float32Array(PARTICLES * 8); // x y z vx vy vz life maxLife
    const particleTint = new Uint8Array(PARTICLES);
    let nextParticle = 0;
    const motes = new Float32Array(MOTES * 4);
    for (let i = 0; i < MOTES; i++) {
        motes[i * 4] = (hash(i) - 0.5) * 13;
        motes[i * 4 + 1] = 0.3 + hash(i + 40) * 3.4;
        motes[i * 4 + 2] = (hash(i + 80) - 0.5) * 9;
        motes[i * 4 + 3] = hash(i + 120) * TAU;
    }

    let cursor = 0;
    let view = null;
    let group = null;
    let groupAmount = 0;
    let alpha = 1;
    const ta = [0, 0, 0], tb = [0, 0, 0], tc = [0, 0, 0], td = [0, 0, 0];

    function setGroup(index, amount = 0, opacity = 1) {
        group = index >= 0 && !PANELS[index].globe ? PANELS[index] : null;
        groupAmount = amount;
        alpha = opacity;
    }
    function place(p, out) {
        if (group && groupAmount > 0) return placePoint(group, groupAmount, p[0], p[1], p[2], out);
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
        return out;
    }
    function raw(x, y, z, color) {
        data[cursor++] = x; data[cursor++] = y; data[cursor++] = z;
        data[cursor++] = color[0]; data[cursor++] = color[1];
        data[cursor++] = color[2]; data[cursor++] = color[3] * alpha;
    }
    const room = vertices => cursor + vertices * 7 <= data.length;
    function rawTri(a, b, c, color) {
        if (!room(3)) return;
        raw(a[0], a[1], a[2], color); raw(b[0], b[1], b[2], color); raw(c[0], c[1], c[2], color);
    }
    function tri(a, b, c, color) {
        place(a, ta); place(b, tb); place(c, tc);
        rawTri(ta, tb, tc, color);
    }
    function quad(a, b, c, d, color) {
        place(a, ta); place(b, tb); place(c, tc); place(d, td);
        rawTri(ta, tb, tc, color);
        rawTri(ta, tc, td, color);
    }
    // A ribbon facing the current camera, so strokes stay visible from any orbit angle.
    function ribbon(a, b, color, width) {
        const dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
        const vx = view.eye[0] - (a[0] + b[0]) * 0.5;
        const vy = view.eye[1] - (a[1] + b[1]) * 0.5;
        const vz = view.eye[2] - (a[2] + b[2]) * 0.5;
        let sx = dy * vz - dz * vy, sy = dz * vx - dx * vz, sz = dx * vy - dy * vx;
        const norm = Math.hypot(sx, sy, sz);
        if (norm < 1e-7 || !room(6)) return;
        const k = width * 0.5 / norm;
        sx *= k; sy *= k; sz *= k;
        raw(a[0] + sx, a[1] + sy, a[2] + sz, color);
        raw(b[0] + sx, b[1] + sy, b[2] + sz, color);
        raw(b[0] - sx, b[1] - sy, b[2] - sz, color);
        raw(a[0] + sx, a[1] + sy, a[2] + sz, color);
        raw(b[0] - sx, b[1] - sy, b[2] - sz, color);
        raw(a[0] - sx, a[1] - sy, a[2] - sz, color);
    }
    function line(a, b, color, width = 0.014) {
        place(a, ta); place(b, tb);
        ribbon(ta, tb, color, width);
    }
    function dot(p, color, radius = 0.042) {
        place(p, tc);
        if (!room(24)) return;
        const { right, up } = view;
        let px = tc[0] + right[0] * radius, py = tc[1] + right[1] * radius, pz = tc[2] + right[2] * radius;
        for (let i = 1; i <= 8; i++) {
            const c = Math.cos(TAU * i / 8) * radius, s = Math.sin(TAU * i / 8) * radius;
            const qx = tc[0] + right[0] * c + up[0] * s;
            const qy = tc[1] + right[1] * c + up[1] * s;
            const qz = tc[2] + right[2] * c + up[2] * s;
            raw(tc[0], tc[1], tc[2], color); raw(px, py, pz, color); raw(qx, qy, qz, color);
            px = qx; py = qy; pz = qz;
        }
    }
    const polar = (x, y, z, r, a) => [x + r * Math.cos(a), y, z + r * Math.sin(a)];
    function arc(x, y, z, r, start, end, color, width = 0.014) {
        const steps = Math.max(6, Math.ceil((end - start) * Math.max(3, Math.min(13, r * 9))));
        let a = polar(x, y, z, r, start);
        for (let i = 1; i <= steps; i++) {
            const b = polar(x, y, z, r, start + (end - start) * i / steps);
            line(a, b, color, width);
            a = b;
        }
    }
    function donut(x, y, z, radius, values, colors) {
        arc(x, y - 0.055, z, radius * 1.20, 0, TAU, GRID);
        arc(x, y - 0.055, z, radius * 1.34, 0, TAU, FAINT);
        arc(x, y, z, radius * 0.72, 0, TAU, GRID, radius * 0.25);
        let start = -Math.PI * 0.5;
        for (let j = 0; j < values.length; j++) {
            const end = start + TAU * values[j];
            if (end - start < 0.055) {
                start = end;
                continue;
            }
            const steps = Math.ceil((end - start) * 11);
            const inner = radius * 0.61;
            const tint = colors[j];
            for (let i = 0; i < steps; i++) {
                const a = start + 0.027 + (end - start - 0.054) * i / steps;
                const b = start + 0.027 + (end - start - 0.054) * (i + 1) / steps;
                const p = polar(x, y, z, radius, a), q = polar(x, y, z, radius, b);
                quad(p, q, polar(x, y, z, inner, b), polar(x, y, z, inner, a), tint);
                quad(p, q, polar(x, y - 0.09, z, radius, b), polar(x, y - 0.09, z, radius, a), GRID);
            }
            arc(x, y + 0.008, z, radius, start + 0.027, end - 0.027, tint, 0.018);
            start = end;
        }
    }
    function cylinder(x, y, z, h, color) {
        const r = 0.115;
        arc(x, y - 0.03, z, r * 1.65, 0, TAU, color, 0.016);
        arc(x, y - 0.035, z, r * 2.45, 0, TAU, GRID);
        const fill = [color[0], color[1], color[2], 0.19];
        for (let i = 0; i < 20; i++) {
            const a = TAU * i / 20, b = TAU * (i + 1) / 20;
            quad(polar(x, y, z, r, a), polar(x, y, z, r, b),
                 polar(x, y + h, z, r, b), polar(x, y + h, z, r, a), fill);
        }
        arc(x, y + h, z, r, 0, TAU, color, 0.026);
        arc(x, y, z, r, 0, TAU, color, 0.017);
        line([x - r, y, z], [x - r, y + h, z], color);
        line([x + r, y, z], [x + r, y + h, z], color);
    }
    function outline(x, y, z, width, length, tint, filled = true, stroke = 0.014) {
        const k = Math.min(0.16, width * 0.2, length * 0.2);
        const points = [
            [x + k, y, z], [x + width - k, y, z], [x + width, y, z + k],
            [x + width, y, z + length - k], [x + width - k, y, z + length],
            [x + k, y, z + length], [x, y, z + length - k], [x, y, z + k],
        ];
        if (filled) {
            for (let i = 1; i < points.length - 1; i++) tri(points[0], points[i], points[i + 1], FAINT);
        }
        for (let i = 0; i < points.length; i++) {
            line(points[i], points[(i + 1) % points.length], tint, stroke);
            dot(points[i], tint, 0.023 + stroke * 0.6);
        }
    }
    const board = (x, y, z, width, length, tint) => outline(x, y, z, width, length, tint);
    function graph(x, y, z, width, amplitude, upright, samples, tint) {
        const point = (u, v) => [x + width * u, y + (upright ? v : 0), z - (upright ? 0 : v)];
        if (upright) {
            for (let i = 0; i <= 4; i++) {
                line(point(0, i * amplitude / 4), point(1, i * amplitude / 4), i === 0 ? CYAN : GRID);
            }
            for (let i = 0; i <= 12; i++) line(point(i / 12, 0), point(i / 12, amplitude), GRID);
        }
        const haze = [tint[0], tint[1], tint[2], 0.17];
        const step = 1 / (samples.length - 1);
        let a = point(0, amplitude * samples[0]);
        for (let i = 1; i < samples.length; i++) {
            const b = point(i * step, amplitude * samples[i]);
            quad(point((i - 1) * step, 0), point(i * step, 0), b, a, haze);
            line(a, b, tint, upright ? 0.027 : 0.018);
            if (upright) dot(b, i === samples.length - 1 ? WHITE : tint, 0.029);
            a = b;
        }
    }

    function burst(x, y, z, count, speed = 4.2) {
        for (let n = 0; n < count; n++) {
            const i = nextParticle;
            nextParticle = (nextParticle + 1) % PARTICLES;
            const a = Math.random() * TAU;
            const lift = Math.random() * 0.9 - 0.15;
            const v = speed * (0.25 + Math.random() * 0.75);
            const o = i * 8;
            particles[o] = x; particles[o + 1] = y; particles[o + 2] = z;
            particles[o + 3] = Math.cos(a) * v; particles[o + 4] = lift * v; particles[o + 5] = Math.sin(a) * v;
            particles[o + 7] = 0.1 + Math.random() * 0.1;
            particles[o + 6] = particles[o + 7];
            particleTint[i] = Math.floor(Math.random() * PARTICLE_COLORS.length);
        }
    }

    function bolt(from, to, seed, strength) {
        const steps = 18;
        let a = from;
        const color = [0.55, 0.92, 1, 0.85 * strength];
        const glow = [0.35, 0.45, 1, 0.20 * strength];
        for (let i = 1; i <= steps; i++) {
            const u = i / steps;
            const jitter = i === steps ? 0 : Math.sin(Math.PI * u) * 0.42;
            const b = [
                from[0] + (to[0] - from[0]) * u + (hash(seed + i) - 0.5) * jitter,
                from[1] + (to[1] - from[1]) * u + Math.sin(Math.PI * u) * 1.1 + (hash(seed + i + 50) - 0.5) * jitter,
                from[2] + (to[2] - from[2]) * u + (hash(seed + i + 90) - 0.5) * jitter,
            ];
            ribbon(a, b, glow, 0.13);
            ribbon(a, b, color, 0.028);
            a = b;
        }
    }

    function centerOf(panel) {
        const b = panelBounds(panel);
        return [(b.x0 + b.x1) * 0.5, b.y, (b.z0 + b.z1) * 0.5];
    }

    const regionColors = [CYAN, BLUE, VIOLET];
    function update(time, dt, dashboard, camera, focus) {
        const { metrics, cards, analytics, regions } = dashboard;
        view = camera;
        cursor = 0;
        const opacity = i => 1 - 0.78 * focus.dim * (1 - smooth(focus.amounts[i]));

        setGroup(6, 0, opacity(6));
        for (let i = 0; i < 4; i++) {
            const radius = 1.33 + i * 0.35;
            const y = 0.32 + (3 - i) * 0.08;
            arc(0, y, 0, radius, 0, TAU, i % 2 ? GRID : BLUE);
            const phase = time * (0.28 + i * 0.09) * (1 + 2.5 * focus.energy) + i * 1.38;
            const tint = i % 2 ? VIOLET : CYAN;
            arc(0, y, 0, radius, phase, phase + 1.9, tint, 0.028);
            arc(0, y, 0, radius, phase + 3.2, phase + 4.1, tint, 0.021);
            dot(polar(0, y, 0, radius, phase + 1.9), WHITE, 0.063);
        }
        for (let i = 0; i < 48; i++) {
            const a = i * TAU / 48;
            line(polar(0, 0.23, 0, 2.50, a), polar(0, 0.23, 0, 2.57 + (i % 4 === 0 ? 0.07 : 0), a), GRID);
        }

        setGroup(4, focus.amounts[4], opacity(4));
        board(-5.4, 0.19, -3.58, 4.37, 0.93, GRID);
        for (const [i, metric] of metrics.entries()) {
            const x = METRIC_X[i];
            cylinder(x, 0.24, -3.14, 2 * metric.value.value, SERIES_COLORS[i]);
            line([x, 0.25, -3.64], [x, 2.25, -3.64], GRID);
        }
        for (let i = 0; i <= 4; i++) {
            const y = 0.25 + 0.50 * i;
            line([-5.27, y, -3.64], [-1.22, y, -3.64], GRID);
        }

        setGroup(1, focus.amounts[1], opacity(1));
        board(1.34, 0.23, -3.50, 4.68, 0.64, BLUE);
        graph(1.49, 0.37, -3.13, 4.39, 1.90, true, analytics, CYAN);
        line([1.49, 0.35, -3.13], [1.49, 2.27, -3.13], CYAN, 0.027);
        dot([1.49, 2.27, -3.13], WHITE, 0.059);

        setGroup(0, focus.amounts[0], opacity(0));
        board(-5.91, 0.23, 1.94, 4.98, 1.89, CYAN);
        line([-3.39, 0.24, 2.02], [-3.39, 0.24, 3.75], GRID);
        line([-5.83, 0.24, 2.88], [-1.02, 0.24, 2.88], GRID);
        for (const [i, card] of cards.entries()) {
            const x = -5.72 + (i % 2) * 2.40;
            const z = 2.74 + Math.floor(i / 2) * 0.87;
            graph(x + 1.08, 0.25, z, 1.04, 0.41, false, card.samples, SERIES_COLORS[i]);
        }
        setGroup(3, focus.amounts[3], opacity(3));
        board(-5.68, 0.22, -1.06, 3.30, 2.60, BLUE);
        setGroup(2, focus.amounts[2], opacity(2));
        board(2.99, 0.22, -1.06, 3.34, 2.60, BLUE);

        setGroup(5, focus.amounts[5], opacity(5));
        board(1.80, 0.25, 1.84, 4.14, 1.96, VIOLET);
        donut(2.90, 0.37, 2.83, 0.69, regions.map(region => region.share), regionColors);
        for (let i = 0; i < regions.length; i++) {
            dot([3.91, 0.37, 2.53 + 0.41 * i], regionColors[i], 0.055);
        }

        // Hover highlight: a pulsing outline lifted slightly above the board.
        if (focus.hover >= 0 && focus.hover !== focus.selected) {
            const panel = PANELS[focus.hover];
            const pulse = 0.55 + 0.45 * Math.sin(time * 7);
            setGroup(focus.hover, focus.amounts[focus.hover], pulse);
            if (panel.globe) {
                arc(0, 0.36, 0, 2.62, 0, TAU, WHITE, 0.03);
                arc(0, 0.36, 0, 2.74 + 0.1 * pulse, 0, TAU, CYAN, 0.018);
            } else {
                outline(panel.x - 0.06, panel.y + 0.03, panel.z - 0.06, panel.w + 0.12, panel.l + 0.12, WHITE, false, 0.026);
            }
        }

        setGroup(-1);
        const selected = focus.selected;
        const amount = selected >= 0 ? smooth(focus.amounts[selected]) : 0;
        if (selected >= 0 && amount > 0.01) {
            const panel = PANELS[selected];
            const b = panelBounds(panel);
            const center = centerOf(panel);
            const w = b.x1 - b.x0, l = b.z1 - b.z0;
            const top = panel.globe ? 0.34 : panel.y + LIFT * amount;
            alpha = amount;
            // Exploded hologram layers stacked under the lifted panel.
            for (let k = 1; k <= 3; k++) {
                alpha = amount * (0.75 - k * 0.17);
                const y = top - k * 0.17 * amount;
                const grow = k * 0.07 * amount;
                outline(b.x0 - grow, y, b.z0 - grow, w + grow * 2, l + grow * 2, k % 2 ? CYAN : VIOLET, false, 0.012);
            }
            // Projector beams from the slab to the floating panel corners.
            if (!panel.globe) {
                alpha = amount * 0.8;
                const beam = [0.25, 0.65, 1, 0.07];
                for (const [cx, cz] of [[b.x0, b.z0], [b.x1, b.z0], [b.x1, b.z1], [b.x0, b.z1]]) {
                    ribbon([cx, 0.16, cz], [cx, top, cz], CYAN, 0.02);
                    ribbon([center[0], 0.16, center[2]], [cx, top, cz], beam, 0.09);
                }
            }
            // Expanding portal rings around the focus target.
            const base = Math.hypot(w, l) * 0.5;
            for (let k = 0; k < 4; k++) {
                const phase = (time * 0.42 + k / 4) % 1;
                alpha = amount * (1 - phase) * 0.5;
                arc(center[0], top - 0.08, center[2], base * (0.75 + phase * 0.9), 0, TAU,
                    k % 2 ? VIOLET : CYAN, 0.02 + 0.03 * (1 - phase));
            }
            // Materialize sweep, then a slow scanner pass.
            const sweeps = [[focus.reveal, 1 - focus.reveal], [(time * 0.23) % 1, 0.35]];
            for (const [at, strength] of sweeps) {
                if (strength <= 0.01 || at <= 0 || at >= 1) continue;
                const z = b.z0 + (b.z1 - b.z0) * at;
                alpha = amount * strength;
                ribbon([b.x0 - 0.15, top + 0.04, z], [b.x1 + 0.15, top + 0.04, z], WHITE, 0.05);
                const band = [0.3, 0.8, 1, 0.12];
                rawTri([b.x0, top + 0.03, z], [b.x1, top + 0.03, z], [b.x1, top + 0.03, Math.max(b.z0, z - 0.45)], band);
                rawTri([b.x0, top + 0.03, z], [b.x1, top + 0.03, Math.max(b.z0, z - 0.45)], [b.x0, top + 0.03, Math.max(b.z0, z - 0.45)], band);
            }
        }

        // Lightning from the globe to the focused panel (or to every panel when the globe is focused).
        if (focus.energy > 0.02 && selected >= 0) {
            alpha = 1;
            const seedBase = Math.floor(time * 17) * 131;
            const globeTop = [0, globeHeight + globeRadius * 0.6, 0];
            const targets = PANELS[selected].globe ? PANELS.slice(0, 6) : [PANELS[selected]];
            for (const [t, panel] of targets.entries()) {
                if (panel.globe) continue;
                const end = centerOf(panel);
                end[1] = panel.y + LIFT * smooth(focus.amounts[PANELS.indexOf(panel)]);
                const dx = end[0], dz = end[2], d = Math.hypot(dx, dz) || 1;
                const start = [globeTop[0] + dx / d * globeRadius * 0.8, globeTop[1], globeTop[2] + dz / d * globeRadius * 0.8];
                const bolts = targets.length > 1 ? 1 : 3;
                for (let k = 0; k < bolts; k++) {
                    const flicker = hash(seedBase + k * 7 + t * 13) > 0.25 ? 1 : 0.25;
                    const jitterEnd = [end[0] + (hash(seedBase + k) - 0.5) * panel.w * 0.6, end[1],
                        end[2] + (hash(seedBase + k + 3) - 0.5) * panel.l * 0.6];
                    bolt(start, jitterEnd, seedBase + k * 37 + t * 211, focus.energy * flicker);
                }
            }
        }

        // Particle shrapnel with drag and a little gravity, drawn as glowing streaks.
        alpha = 1;
        const drag = Math.exp(-dt * 1.6);
        for (let i = 0; i < PARTICLES; i++) {
            const o = i * 8;
            if (particles[o + 6] <= 0) continue;
            particles[o + 6] -= dt;
            particles[o + 3] *= drag; particles[o + 4] = particles[o + 4] * drag - dt * 1.4; particles[o + 5] *= drag;
            particles[o] += particles[o + 3] * dt;
            particles[o + 1] = Math.max(0.2, particles[o + 1] + particles[o + 4] * dt);
            particles[o + 2] += particles[o + 5] * dt;
            const life = Math.max(0, particles[o + 6] / particles[o + 7]);
            const base = PARTICLE_COLORS[particleTint[i]];
            const color = [base[0], base[1], base[2], life];
            ta[0] = particles[o]; ta[1] = particles[o + 1]; ta[2] = particles[o + 2];
            tb[0] = ta[0] - particles[o + 3] * 0.06; tb[1] = ta[1] - particles[o + 4] * 0.06; tb[2] = ta[2] - particles[o + 5] * 0.06;
            ribbon(ta, tb, color, 0.035);
        }

        // Ambient dust drifting through the projector light.
        for (let i = 0; i < MOTES; i++) {
            const o = i * 4;
            const y = 0.3 + ((motes[o + 1] + time * 0.07 * (0.5 + hash(i))) % 3.6);
            const x = motes[o] + Math.sin(time * 0.3 + motes[o + 3]) * 0.3;
            const z = motes[o + 2] + Math.cos(time * 0.24 + motes[o + 3]) * 0.3;
            const glint = 0.12 + 0.35 * Math.max(0, Math.sin(time * 1.7 + motes[o + 3] * 3));
            dot([x, y, z], [0.55, 0.8, 1, glint * (1 - (y - 0.3) / 3.6)], 0.016);
        }

        gl.updateBuffer(buffer, data.subarray(0, cursor));
    }

    return { layout, globeLayout, globeCount: sphere.length / 6, update, burst,
        get count() { return cursor / 7; } };
}
