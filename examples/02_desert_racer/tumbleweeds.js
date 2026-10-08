// Tumbleweeds rolling downwind across the dunes. Each one rolls on the sand,
// hops now and then, and flies off when a car hits it. They respawn upwind of
// the player, so a few are always around. Drawn with the palm program.

const WIND_X = 0.94;
const WIND_Y = 0.34;
const RADIUS = 0.75;
const GRAVITY = 9.0;

// A ball of twigs: thin sticks along random chords of a sphere, each made of
// two crossed ribbons so it never vanishes edge-on, with straw colors.
export function makeTumbleweedMesh(seed) {
    let state = (seed * 7919 + 13) >>> 0;
    const rand = () => {
        state = (Math.imul(state, 1103515245) + 12345) >>> 0;
        return state / 4294967296;
    };
    const onSphere = (radius) => {
        const z = rand() * 2 - 1, a = rand() * Math.PI * 2, r = Math.sqrt(1 - z * z);
        return [Math.cos(a) * r * radius, Math.sin(a) * r * radius, z * radius];
    };
    const positions = [], normals = [], colors = [], indices = [];
    const STICKS = 70;
    for (let i = 0; i < STICKS; i++) {
        const a = onSphere(RADIUS * (0.75 + rand() * 0.3));
        const b = onSphere(RADIUS * (0.75 + rand() * 0.3));
        let dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
        const len = Math.hypot(dx, dy, dz) || 1;
        dx /= len; dy /= len; dz /= len;
        // Two directions across the stick, perpendicular to it and each other.
        let px = dy, py = -dx, pz = 0;
        if (Math.hypot(px, py) < 0.1) { px = 0; py = dz; pz = -dy; }
        let pl = Math.hypot(px, py, pz);
        px /= pl; py /= pl; pz /= pl;
        const qx = dy * pz - dz * py, qy = dz * px - dx * pz, qz = dx * py - dy * px;
        const width = 0.025 + rand() * 0.025;
        const shade = 0.7 + rand() * 0.45;
        const color = [0.60 * shade, 0.46 * shade, 0.28 * shade];
        // Outward normals light the ball like a rough sphere.
        const mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2, mz = (a[2] + b[2]) / 2;
        const ml = Math.hypot(mx, my, mz) || 1;
        for (const [ux, uy, uz] of [[px, py, pz], [qx, qy, qz]]) {
            const base = positions.length / 3;
            for (const [p, side] of [[a, -1], [a, 1], [b, 1], [b, -1]]) {
                positions.push(p[0] + ux * width * side, p[1] + uy * width * side, p[2] + uz * width * side);
                normals.push(mx / ml, my / ml, mz / ml);
                colors.push(color[0], color[1], color[2]);
            }
            indices.push(base, base + 1, base + 2, base, base + 2, base + 3);
        }
    }
    return {
        positions: new Float32Array(positions),
        normals: new Float32Array(normals),
        colors: new Float32Array(colors),
        indices: new Uint16Array(indices),
    };
}

export function createTumbleweeds(count, getHeight) {
    const weeds = [];
    for (let i = 0; i < count; i++) {
        weeds.push({
            x: 0, y: 0, z: -1000, vx: 0, vy: 0, vz: 0,
            q: new Float32Array([0, 0, 0, 1]),
            pace: 3.5 + Math.random() * 3.5,
            hopIn: Math.random() * 2,
            hitCooldown: 0,
            variant: i,
            placed: false,
        });
    }
    const dq = new Float32Array(4);
    const axis = new Float32Array(3);
    const rotation = new Float32Array(16);

    function respawn(weed, player, first) {
        // Upwind of the player, off to a side, so it rolls across the view.
        const ahead = first ? 10 + Math.random() * 40 : 45 + Math.random() * 20;
        const side = (Math.random() - 0.5) * 70;
        weed.x = player.x - WIND_X * ahead - WIND_Y * side;
        weed.y = player.y - WIND_Y * ahead + WIND_X * side;
        weed.z = getHeight(weed.x, weed.y) + RADIUS;
        weed.vx = WIND_X * weed.pace;
        weed.vy = WIND_Y * weed.pace;
        weed.vz = 0;
        weed.placed = true;
    }

    // cars: all cars; onHit(weed) when a car knocks one away.
    function update(dt, player, cars, onHit) {
        if (dt <= 0) return;
        for (const weed of weeds) {
            if (!weed.placed) respawn(weed, player, true);
            const dx = weed.x - player.x, dy = weed.y - player.y;
            // Gone downwind and out of sight: back upwind.
            if (dx * dx + dy * dy > 75 * 75) respawn(weed, player, false);

            const ground = getHeight(weed.x, weed.y) + RADIUS;
            const onGround = weed.z <= ground + 0.05;
            // The wind pushes it back to its pace; the sand slows it down.
            const push = 1 - Math.exp(-(onGround ? 1.2 : 0.3) * dt);
            weed.vx += (WIND_X * weed.pace - weed.vx) * push;
            weed.vy += (WIND_Y * weed.pace - weed.vy) * push;
            weed.vz -= GRAVITY * dt;
            weed.x += weed.vx * dt;
            weed.y += weed.vy * dt;
            weed.z += weed.vz * dt;
            if (weed.z < ground) {
                weed.z = ground;
                weed.vz = Math.abs(weed.vz) * 0.35;
                weed.hopIn -= dt;
                if (weed.hopIn <= 0) {
                    weed.vz += 2.2 + Math.random() * 2.6;
                    weed.hopIn = 0.8 + Math.random() * 1.8;
                }
            }

            weed.hitCooldown = Math.max(0, weed.hitCooldown - dt);
            for (const car of cars) {
                if (weed.hitCooldown > 0) break;
                const cx = weed.x - car.x, cy = weed.y - car.y;
                const dist = Math.hypot(cx, cy);
                if (dist > RADIUS + 1.6 || Math.abs(weed.z - car.z) > 2.5) continue;
                const speed = Math.hypot(car.vx, car.vy);
                if (speed < 2) continue;
                weed.vx = car.vx * 1.1 + (cx / (dist || 1)) * 3;
                weed.vy = car.vy * 1.1 + (cy / (dist || 1)) * 3;
                weed.vz = 4 + speed * 0.18;
                weed.z += 0.3;
                weed.hitCooldown = 0.6;
                if (onHit) onHit(weed);
            }

            // Roll: turn about the horizontal axis across the motion.
            const planar = Math.hypot(weed.vx, weed.vy);
            if (planar > 0.01) {
                axis[0] = -weed.vy / planar;
                axis[1] = weed.vx / planar;
                axis[2] = 0;
                sys.math.quatFromAxisAngle(dq, axis, planar / RADIUS * dt);
                sys.math.quatMultiply(weed.q, dq, weed.q);
                const ql = Math.hypot(weed.q[0], weed.q[1], weed.q[2], weed.q[3]) || 1;
                for (let k = 0; k < 4; k++) weed.q[k] /= ql;
            }
        }
    }

    // fn(model, variant) for each tumbleweed.
    function forEach(fn, model) {
        for (const weed of weeds) {
            sys.math.quatToMat4(rotation, weed.q);
            model.set(rotation);
            model[12] = weed.x;
            model[13] = weed.y;
            model[14] = weed.z;
            fn(model, weed.variant);
        }
    }

    return { update, forEach };
}
