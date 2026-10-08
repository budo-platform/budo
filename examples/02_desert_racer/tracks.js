// Tyre tracks: each rear wheel of a grounded car lays a ribbon of short quads
// on the sand. Segments live in a ring buffer and fade out with age in the
// vertex shader; only the segments added each frame are uploaded.

import { CAR_LENGTH, CAR_WIDTH, physics } from './car_physics.js';

const FLOATS_PER_VERTEX = 8; // position(3) + across, along + birth, opacity, unused
const SEGMENT_FLOATS = 4 * FLOATS_PER_VERTEX;
const SEGMENT_LENGTH = 0.7;  // world units between track points
const LIFT = 0.05;           // above the sand, against z-fighting

export function createTracks(maxSegments, getHeight) {
    const vertices = new Float32Array(maxSegments * SEGMENT_FLOATS);
    const indices = new Uint16Array(maxSegments * 6);
    for (let i = 0; i < maxSegments; i++) {
        const v = i * 4;
        indices.set([v, v + 1, v + 2, v, v + 2, v + 3], i * 6);
    }
    const vbo = sys.gl.createBuffer('vertex', vertices, 'dynamic');
    const ibo = sys.gl.createBuffer('index', indices);
    const layout = sys.gl.createVertexLayout();
    const stride = FLOATS_PER_VERTEX * 4;
    sys.gl.setAttribute(layout, 0 /* a_position */, vbo, 3, 'float', false, stride, 0);
    sys.gl.setAttribute(layout, 1 /* a_uv       */, vbo, 2, 'float', false, stride, 3 * 4);
    sys.gl.setAttribute(layout, 3 /* a_color    */, vbo, 3, 'float', false, stride, 5 * 4);
    sys.gl.setIndexBuffer(layout, ibo, 'u16');
    const program = sys.gl.createProgram('tracks.vert', 'tracks.frag');

    let next = 0;          // ring cursor
    let used = 0;          // segments ever written, capped at maxSegments
    let dirtyFrom = -1;    // first segment index written since the last upload
    let dirtyCount = 0;
    const wheels = new Map(); // car -> [left, right] track ends

    function writeVertex(o, x, y, z, across, along, born, opacity) {
        vertices[o] = x; vertices[o + 1] = y; vertices[o + 2] = z;
        vertices[o + 3] = across; vertices[o + 4] = along;
        vertices[o + 5] = born; vertices[o + 6] = opacity; vertices[o + 7] = 0;
    }

    function addSegment(a, b, halfWidth, now, opacity) {
        const dx = b.x - a.x, dy = b.y - a.y;
        const len = Math.hypot(dx, dy) || 1;
        const nx = -dy / len * halfWidth, ny = dx / len * halfWidth;
        const i = next;
        const o = i * SEGMENT_FLOATS;
        const za = getHeight(a.x, a.y) + LIFT, zb = getHeight(b.x, b.y) + LIFT;
        writeVertex(o, a.x - nx, a.y - ny, za, -1, a.along, now, a.opacity);
        writeVertex(o + FLOATS_PER_VERTEX, a.x + nx, a.y + ny, za, 1, a.along, now, a.opacity);
        writeVertex(o + 2 * FLOATS_PER_VERTEX, b.x + nx, b.y + ny, zb, 1, b.along, now, opacity);
        writeVertex(o + 3 * FLOATS_PER_VERTEX, b.x - nx, b.y - ny, zb, -1, b.along, now, opacity);
        if (dirtyCount === 0) dirtyFrom = i;
        dirtyCount++;
        next = (next + 1) % maxSegments;
        used = Math.min(maxSegments, used + 1);
    }

    // Extend this car's two rear-wheel tracks; now in seconds.
    function update(car, now) {
        let ends = wheels.get(car);
        const speed = Math.hypot(car.vx, car.vy);
        if (!car.grounded || speed < 0.8) {
            if (ends) ends[0] = ends[1] = null; // lift off: the tracks break
            return;
        }
        if (!ends) {
            ends = [null, null];
            wheels.set(car, ends);
        }
        const cosH = Math.cos(car.heading), sinH = Math.sin(car.heading);
        const scale = physics.vehicleScale;
        const back = CAR_LENGTH * scale * 0.40;
        const side = CAR_WIDTH * scale * 0.40;
        const sideSpeed = Math.abs(car.vx * sinH - car.vy * cosH);
        const opacity = Math.min(0.75, 0.32 + sideSpeed * 0.06);
        const halfWidth = CAR_WIDTH * scale * 0.11;
        for (let k = 0; k < 2; k++) {
            const wheel = k === 0 ? -1 : 1;
            const x = car.x - cosH * back + sinH * side * wheel;
            const y = car.y - sinH * back - cosH * side * wheel;
            const end = ends[k];
            if (!end) {
                ends[k] = { x, y, along: 0, opacity };
                continue;
            }
            const dist = Math.hypot(x - end.x, y - end.y);
            if (dist < SEGMENT_LENGTH) continue;
            const point = { x, y, along: end.along + dist, opacity };
            if (dist < SEGMENT_LENGTH * 6) addSegment(end, point, halfWidth, now, opacity);
            ends[k] = point; // a teleport or a long jump starts a new track
        }
    }

    function upload() {
        if (dirtyCount === 0) return;
        // The written range may wrap around the end of the ring.
        const first = Math.min(dirtyCount, maxSegments - dirtyFrom);
        sys.gl.updateBuffer(vbo, vertices.subarray(dirtyFrom * SEGMENT_FLOATS, (dirtyFrom + first) * SEGMENT_FLOATS),
            dirtyFrom * SEGMENT_FLOATS * 4);
        const rest = Math.min(dirtyCount - first, maxSegments);
        if (rest > 0) sys.gl.updateBuffer(vbo, vertices.subarray(0, rest * SEGMENT_FLOATS), 0);
        dirtyCount = 0;
    }

    // scene: { viewProj, eye, now, color, fogStart, fogEnd }.
    function draw(scene) {
        upload();
        if (used === 0) return;
        sys.gl.setUniformMatrix4(program, 'u_mvp', scene.viewProj);
        sys.gl.setUniform1f(program, 'u_now', scene.now);
        sys.gl.setUniform3fv(program, 'u_eye', scene.eye);
        sys.gl.setUniform3fv(program, 'u_track_color', scene.color);
        sys.gl.setUniform1f(program, 'u_fog_start', scene.fogStart);
        sys.gl.setUniform1f(program, 'u_fog_end', scene.fogEnd);
        sys.gl.drawMesh(program, layout, {
            mode: 'triangles',
            first: 0,
            count: used * 6,
            depthTest: true,
            depthWrite: false,
            cull: 'none',
            blend: 'alpha',
        });
    }

    return { update, draw };
}
