// Sand dust kicked up by the wheels: soft camera-facing puffs, simulated on
// the CPU and drawn in one batch. Trails grow with speed and sliding, and a
// landing throws a ring of dust around the car.

import { CAR_LENGTH, CAR_WIDTH, physics } from './car_physics.js';

const FLOATS_PER_VERTEX = 8; // position(3) + corner(2) + size, alpha, seed
const CORNERS = [-1, -1, 1, -1, 1, 1, -1, 1];
const WIND_X = 0.7;
const WIND_Y = 0.25;

export function createDust(maxParticles) {
    const px = new Float32Array(maxParticles);
    const py = new Float32Array(maxParticles);
    const pz = new Float32Array(maxParticles);
    const vx = new Float32Array(maxParticles);
    const vy = new Float32Array(maxParticles);
    const vz = new Float32Array(maxParticles);
    const floor = new Float32Array(maxParticles);
    const age = new Float32Array(maxParticles);
    const life = new Float32Array(maxParticles); // 0: free slot
    const size = new Float32Array(maxParticles);
    const grow = new Float32Array(maxParticles);
    const opacity = new Float32Array(maxParticles);
    const seed = new Float32Array(maxParticles);

    const vertices = new Float32Array(maxParticles * 4 * FLOATS_PER_VERTEX);
    const indices = new Uint16Array(maxParticles * 6);
    for (let i = 0; i < maxParticles; i++) {
        const v = i * 4;
        indices.set([v, v + 1, v + 2, v, v + 2, v + 3], i * 6);
        for (let corner = 0; corner < 4; corner++) {
            const o = (v + corner) * FLOATS_PER_VERTEX;
            vertices[o + 3] = CORNERS[corner * 2];
            vertices[o + 4] = CORNERS[corner * 2 + 1];
        }
    }
    const vbo = sys.gl.createBuffer('vertex', vertices);
    const ibo = sys.gl.createBuffer('index', indices);
    const layout = sys.gl.createVertexLayout();
    const stride = FLOATS_PER_VERTEX * 4;
    sys.gl.setAttribute(layout, 0 /* a_position */, vbo, 3, 'float', false, stride, 0);
    sys.gl.setAttribute(layout, 1 /* a_uv: corner */, vbo, 2, 'float', false, stride, 3 * 4);
    sys.gl.setAttribute(layout, 3 /* a_color: size, alpha, seed */, vbo, 3, 'float', false, stride, 5 * 4);
    sys.gl.setIndexBuffer(layout, ibo, 'u16');
    const program = sys.gl.createProgram('dust.vert', 'dust.frag');

    let next = 0;
    let liveCount = 0;
    const carEmitDebt = new Map();

    function spawn(x, y, z, svx, svy, svz, lifetime, startSize, growth, alpha) {
        const i = next;
        next = (next + 1) % maxParticles; // the oldest puff gives way
        px[i] = x; py[i] = y; pz[i] = z;
        vx[i] = svx; vy[i] = svy; vz[i] = svz;
        floor[i] = z - 0.05;
        age[i] = 0;
        life[i] = lifetime;
        size[i] = startSize;
        grow[i] = growth;
        opacity[i] = alpha;
        seed[i] = Math.random();
    }

    // Continuous trail from the rear wheels of a grounded car.
    function emitTrail(car, dt, groundZ, rate) {
        const speed = Math.hypot(car.vx, car.vy);
        if (!car.grounded || speed < 1.5) {
            carEmitDebt.set(car, 0);
            return;
        }
        const cosH = Math.cos(car.heading), sinH = Math.sin(car.heading);
        const sideSpeed = car.vx * sinH - car.vy * cosH;
        const slip = Math.min(1, Math.abs(sideSpeed) / 6);
        const perSecond = Math.min(1, speed / 26) * rate * (0.4 + slip * 1.4);
        let debt = (carEmitDebt.get(car) || 0) + perSecond * dt;
        const scale = physics.vehicleScale;
        const back = CAR_LENGTH * scale * 0.42;
        const side = CAR_WIDTH * scale * 0.42;
        while (debt >= 1) {
            debt -= 1;
            const wheel = Math.random() < 0.5 ? -1 : 1;
            const x = car.x - cosH * back + sinH * side * wheel;
            const y = car.y - sinH * back - cosH * side * wheel;
            const kick = 1.5 + speed * 0.10;
            spawn(x + (Math.random() - 0.5) * 0.4,
                y + (Math.random() - 0.5) * 0.4,
                groundZ + 0.12,
                car.vx * 0.22 - cosH * kick + (Math.random() - 0.5) * 2.4 + sinH * wheel * slip * 2.0,
                car.vy * 0.22 - sinH * kick + (Math.random() - 0.5) * 2.4 - cosH * wheel * slip * 2.0,
                0.5 + Math.random() * 0.9 + slip,
                0.7 + Math.random() * 0.8 + slip * 1.4,
                0.35 + Math.random() * 0.25,
                0.5 + Math.random() * 0.4 + slip * 0.9,
                0.11 + slip * 0.17);
        }
        carEmitDebt.set(car, debt);
    }

    // A ring of dust around a landing car, carried along a little by the
    // car's momentum; strength 0..1.
    function burst(car, groundZ, strength, count) {
        for (let k = 0; k < count; k++) {
            const a = (k / count) * Math.PI * 2 + Math.random() * 0.3;
            const out = 3.0 + strength * 6.0 * (0.5 + Math.random() * 0.5);
            spawn(car.x + Math.cos(a) * 1.0, car.y + Math.sin(a) * 1.0, groundZ + 0.1,
                car.vx * 0.45 + Math.cos(a) * out, car.vy * 0.45 + Math.sin(a) * out,
                0.6 + Math.random() * 1.8 * strength,
                1.2 + Math.random() * 1.0,
                0.5 + Math.random() * 0.4,
                0.9 + strength * 1.0,
                0.26 + strength * 0.2);
        }
    }

    function update(dt) {
        const drag = Math.exp(-1.7 * dt);
        let out = 0;
        for (let i = 0; i < maxParticles; i++) {
            if (life[i] <= 0) continue;
            age[i] += dt;
            const t = age[i] / life[i];
            if (t >= 1) {
                life[i] = 0;
                continue;
            }
            vx[i] = vx[i] * drag + WIND_X * (1 - drag);
            vy[i] = vy[i] * drag + WIND_Y * (1 - drag);
            vz[i] = vz[i] * drag - 0.55 * dt;
            px[i] += vx[i] * dt;
            py[i] += vy[i] * dt;
            pz[i] += vz[i] * dt;
            if (pz[i] < floor[i]) {
                pz[i] = floor[i];
                vz[i] = Math.abs(vz[i]) * 0.2;
            }
            const radius = size[i] + grow[i] * age[i];
            // Quick fade in, long fade out.
            const alpha = opacity[i] * Math.min(1, t * 8) * (1 - t) * (1 - t);
            let o = out * 4 * FLOATS_PER_VERTEX;
            for (let corner = 0; corner < 4; corner++) {
                vertices[o] = px[i];
                vertices[o + 1] = py[i];
                vertices[o + 2] = pz[i] + radius * 0.35;
                vertices[o + 5] = radius;
                vertices[o + 6] = alpha;
                vertices[o + 7] = seed[i];
                o += FLOATS_PER_VERTEX;
            }
            out++;
        }
        liveCount = out;
    }

    // scene: { viewProj, view, eye, light uniforms, headlight }.
    function draw(scene) {
        if (liveCount === 0) return;
        sys.gl.updateBuffer(vbo, vertices.subarray(0, liveCount * 4 * FLOATS_PER_VERTEX), 0);
        const view = scene.view;
        sys.gl.setUniformMatrix4(program, 'u_mvp', scene.viewProj);
        sys.gl.setUniform3f(program, 'u_cam_right', view[0], view[4], view[8]);
        sys.gl.setUniform3f(program, 'u_cam_up', view[1], view[5], view[9]);
        sys.gl.setUniform3fv(program, 'u_eye', scene.eye);
        sys.gl.setUniform3fv(program, 'u_dust_color', scene.dustColor);
        sys.gl.setUniform3fv(program, 'u_light_dir', scene.lightDir);
        sys.gl.setUniform3fv(program, 'u_light_color', scene.lightColor);
        sys.gl.setUniform1f(program, 'u_ambient', scene.ambient);
        sys.gl.setUniform3fv(program, 'u_sun_dir', scene.sunDir);
        sys.gl.setUniform3fv(program, 'u_sun_color', scene.sunColor);
        sys.gl.setUniform3fv(program, 'u_fog_color', scene.fogColor);
        sys.gl.setUniform1f(program, 'u_fog_start', scene.fogStart);
        sys.gl.setUniform1f(program, 'u_fog_end', scene.fogEnd);
        sys.gl.setUniform3fv(program, 'u_head_pos', scene.headPos);
        sys.gl.setUniform3fv(program, 'u_head_dir', scene.headDir);
        sys.gl.setUniform1f(program, 'u_head_on', scene.headOn);
        sys.gl.setUniform2f(program, 'u_near_fade', scene.nearFadeFrom, scene.nearFadeTo);
        sys.gl.drawMesh(program, layout, {
            mode: 'triangles',
            first: 0,
            count: liveCount * 6,
            depthTest: true,
            depthWrite: false,
            cull: 'none',
            blend: 'alpha',
        });
    }

    return { emitTrail, burst, update, draw, get liveCount() { return liveCount; } };
}
