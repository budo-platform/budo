// Vehicle dynamics for the terrain_physics example.
//
// Realtime gameplay model: height-field suspension, speed-sensitive steering,
// tire slip/friction budgeting, yaw inertia, stable ground adhesion, and true
// airborne inertia. It is intentionally deterministic and allocation-light so
// it can run every frame in QuickJS without a full rigid-body solver.

export const CAR_LENGTH = 1.5;
export const CAR_WIDTH = 0.8;
export const CAR_HEIGHT = 0.5;

const AI_DECISION_INTERVAL = 4.0; // seconds between AI re-rolls

// The physics state stores `z` as the terrain/chassis contact reference used
// by the renderer. The model samples four support points around that reference
// to decide whether tires can still reach the ground.
const WHEELBASE = CAR_LENGTH * 0.92;
const TRACK = CAR_WIDTH * 0.96;
const SUSPENSION_TRAVEL = 0.78;
const GROUND_SNAP_EPS = 0.06;
const TAKEOFF_SPEED = 21.0;
const TAKEOFF_VZ_THRESHOLD = 3.1;
const LANDING_RESTITUTION = 0.18;
const REST_VZ_EPS = 0.7;

// Tire / chassis constants. Tuned for an arcade-realistic feel: cars stick to
// rolling terrain, drift a little at speed, but cannot turn or accelerate in air.
const MAX_STEER_ANGLE = 0.68;
const LOW_SPEED_STEER = 0.66;
const TIRE_GRIP = 13.8;
const LATERAL_STIFFNESS = 8.9;
const LONGITUDINAL_GRIP = 1.48;
const YAW_RESPONSE = 13.5;
const YAW_DAMPING_GROUND = 4.8;
const YAW_DAMPING_AIR = 0.35;
const AIR_ANGULAR_DAMPING = 0.55;
const BODY_SETTLE_RATE = 9.5;
const SUSPENSION_FOLLOW_RATE = 16.0;
const SLOPE_VELOCITY_FOLLOW_RATE = 7.0;
const VEHICLE_COLLISION_RESTITUTION = 0.18;
const VEHICLE_COLLISION_FRICTION = 0.32;
const VEHICLE_COLLISION_SLOP = 0.03;
const VEHICLE_COLLISION_POSITION_CORRECTION = 0.72;
const TREE_COLLISION_RESTITUTION = 0.08;
const TREE_COLLISION_FRICTION = 0.55;

// ============ Tunable physics parameters ============
// Mutated at runtime via the on-screen panel and keyboard.

export const physicsDefaults = {
    mass: 1.0,
    gravity: 9.81,
    moveForce: 1800,
    linearDrag: 0.58,  // velocity-proportional drag (1/s)
    rollingFriction: 1.75,  // constant decel when grounded (m/s²)
    maxSpeed: 200,   // safety clamp (m/s)
    turnRate: 4.25,  // yaw authority multiplier
    vehicleScale: 2.3,   // visual scale applied to all car meshes
};
export const physics = { ...physicsDefaults };

// Order shown in the panel; defines digit shortcuts (1..N).
export const physicsParams = [
    { key: 'mass', label: 'Mass', min: 0.1, max: 20, step: 0.5 },
    { key: 'gravity', label: 'Gravity', min: 0, max: 30, step: 0.5 },
    { key: 'moveForce', label: 'Throttle', min: 0, max: 10000, step: 1.0 },
    { key: 'linearDrag', label: 'Air drag', min: 0, max: 5, step: 0.05 },
    { key: 'rollingFriction', label: 'Friction', min: 0, max: 10, step: 0.1 },
    { key: 'maxSpeed', label: 'Max speed', min: 1, max: 10000, step: 10.0 },
    { key: 'turnRate', label: 'Turn rate', min: 0.2, max: 8, step: 0.1 },
    { key: 'vehicleScale', label: 'Car size', min: 0.5, max: 2.5, step: 0.1 },
];

export function clampParam(def, value) {
    return Math.max(def.min, Math.min(def.max, value));
}

export function adjustParam(index, delta) {
    const def = physicsParams[index];
    if (!def) return;
    physics[def.key] = clampParam(def, physics[def.key] + delta);
}

export function resetPhysics() {
    Object.assign(physics, physicsDefaults);
}

export function makeCar(opts) {
    return {
        x: opts.x ?? 0,
        y: opts.y ?? 0,
        z: opts.z ?? 0,
        heading: opts.heading ?? 0,
        vx: 0, vy: 0, vz: 0,
        grounded: true,
        airborneTime: 0,
        pitch: 0,
        roll: 0,
        pitchRate: 0,
        rollRate: 0,
        yawRate: 0,
        contactCount: 4,
        groundZ: opts.z ?? 0,
        compression: 1,
        color: opts.color ?? new Float32Array([0.85, 0.20, 0.20]),
        mesh: opts.mesh ?? null,
        isAI: !!opts.isAI,
        aiTimer: 0,
        aiThrottle: 0,
        aiSteer: 0,
    };
}

export function pickAIControls(car) {
    // Throttle biased forward so the AI car actually goes somewhere.
    car.aiThrottle = 0.55 + Math.random() * 0.45;        // 0.55..1.0
    car.aiSteer = (Math.random() * 2 - 1) * 0.9;      // -0.9..0.9
    car.aiTimer = AI_DECISION_INTERVAL;
}

export function gatherAIControls(car, dt) {
    car.aiTimer -= dt;
    if (car.aiTimer <= 0) pickAIControls(car);
    return { throttle: car.aiThrottle, steer: car.aiSteer };
}

function clamp(x, lo, hi) { return Math.max(lo, Math.min(hi, x)); }
function clamp01(x) { return clamp(x, 0, 1); }
function approach(current, target, rate, dt) {
    return current + (target - current) * (1 - Math.exp(-rate * dt));
}
function applyFriction(value, decel) {
    if (Math.abs(value) <= decel) return 0;
    return value - Math.sign(value) * decel;
}

function sampleSupport(car, terrain, fwdX, fwdY, rightX, rightY) {
    const halfWheelbase = WHEELBASE * 0.5;
    const halfTrack = TRACK * 0.5;
    const wheelOffsets = [
        [halfWheelbase, halfTrack],
        [halfWheelbase, -halfTrack],
        [-halfWheelbase, halfTrack],
        [-halfWheelbase, -halfTrack],
    ];

    let contactCount = 0;
    let weightSum = 0;
    let supportZ = 0;
    let maxZ = -Infinity;
    let nx = 0, ny = 0, nz = 0;
    let compressionSum = 0;

    for (const [ox, oy] of wheelOffsets) {
        const wx = car.x + fwdX * ox + rightX * oy;
        const wy = car.y + fwdY * ox + rightY * oy;
        const h = terrain.getHeight(wx, wy);
        const n = terrain.getNormal(wx, wy);
        const heightAbove = car.z - h;
        const compression = clamp01((SUSPENSION_TRAVEL - Math.max(0, heightAbove)) / SUSPENSION_TRAVEL);
        const canReach = heightAbove <= SUSPENSION_TRAVEL;

        if (canReach) {
            contactCount++;
            const w = 0.20 + compression;
            weightSum += w;
            supportZ += h * w;
            compressionSum += compression;
            nx += n.x * w;
            ny += n.y * w;
            nz += n.z * w;
        }
        maxZ = Math.max(maxZ, h);
    }

    if (weightSum > 1e-5) {
        supportZ /= weightSum;
        const nl = Math.sqrt(nx * nx + ny * ny + nz * nz) || 1;
        nx /= nl; ny /= nl; nz /= nl;
    } else {
        supportZ = terrain.getHeight(car.x, car.y);
        const n = terrain.getNormal(car.x, car.y);
        nx = n.x; ny = n.y; nz = n.z;
    }

    return {
        contactCount,
        supportZ: Math.max(supportZ, maxZ - 0.28),
        compression: contactCount ? compressionSum / contactCount : 0,
        normal: { x: nx, y: ny, z: nz },
    };
}

function vehicleCollisionRadius() {
    const scale = physics.vehicleScale;
    return Math.max(CAR_LENGTH * scale * 0.45, CAR_WIDTH * scale * 0.76);
}

export function resolveVehicleCollisions(cars) {
    const radius = vehicleCollisionRadius();
    const minDist = radius * 2;
    const minDist2 = minDist * minDist;
    const invMass = 1 / Math.max(0.1, physics.mass);

    for (let i = 0; i < cars.length; i++) {
        const a = cars[i];
        for (let j = i + 1; j < cars.length; j++) {
            const b = cars[j];
            let dx = a.x - b.x;
            let dy = a.y - b.y;
            let dist2 = dx * dx + dy * dy;
            if (dist2 >= minDist2) continue;

            if (dist2 < 1e-6) {
                dx = Math.cos(a.heading);
                dy = Math.sin(a.heading);
                dist2 = 1;
            }

            const dist = Math.sqrt(dist2);
            const nx = dx / dist;
            const ny = dy / dist;
            const penetration = minDist - dist;

            // Positional solve first to prevent cars from interpenetrating and
            // repeatedly injecting impulse energy on following frames.
            const correction = Math.max(0, penetration - VEHICLE_COLLISION_SLOP)
                * VEHICLE_COLLISION_POSITION_CORRECTION * 0.5;
            a.x += nx * correction;
            a.y += ny * correction;
            b.x -= nx * correction;
            b.y -= ny * correction;

            const rvx = a.vx - b.vx;
            const rvy = a.vy - b.vy;
            const vn = rvx * nx + rvy * ny;
            if (vn < 0) {
                const impulse = -(1 + VEHICLE_COLLISION_RESTITUTION) * vn / (invMass + invMass);
                const ix = impulse * nx;
                const iy = impulse * ny;
                a.vx += ix * invMass;
                a.vy += iy * invMass;
                b.vx -= ix * invMass;
                b.vy -= iy * invMass;

                const tx = -ny;
                const ty = nx;
                const vt = rvx * tx + rvy * ty;
                const maxFriction = impulse * VEHICLE_COLLISION_FRICTION;
                const frictionImpulse = clamp(-vt / (invMass + invMass), -maxFriction, maxFriction);
                const fix = frictionImpulse * tx;
                const fiy = frictionImpulse * ty;
                a.vx += fix * invMass;
                a.vy += fiy * invMass;
                b.vx -= fix * invMass;
                b.vy -= fiy * invMass;

                const spin = clamp(vt * 0.035, -0.55, 0.55);
                a.yawRate += spin;
                b.yawRate -= spin;
            }
        }
    }
}

export function resolveTreeCollision(car, treeX, treeY, treeRadius) {
    const carRadius = vehicleCollisionRadius();
    const minDist = carRadius + treeRadius;
    let dx = car.x - treeX;
    let dy = car.y - treeY;
    let dist2 = dx * dx + dy * dy;
    if (dist2 >= minDist * minDist) return false;

    if (dist2 < 1e-6) {
        dx = Math.cos(car.heading);
        dy = Math.sin(car.heading);
        dist2 = 1;
    }

    const dist = Math.sqrt(dist2);
    const nx = dx / dist;
    const ny = dy / dist;
    const penetration = minDist - dist;
    car.x += nx * Math.max(0, penetration - VEHICLE_COLLISION_SLOP) * 0.92;
    car.y += ny * Math.max(0, penetration - VEHICLE_COLLISION_SLOP) * 0.92;

    const vn = car.vx * nx + car.vy * ny;
    if (vn < 0) {
        car.vx -= (1 + TREE_COLLISION_RESTITUTION) * vn * nx;
        car.vy -= (1 + TREE_COLLISION_RESTITUTION) * vn * ny;

        const tx = -ny;
        const ty = nx;
        const vt = car.vx * tx + car.vy * ty;
        car.vx -= tx * vt * TREE_COLLISION_FRICTION;
        car.vy -= ty * vt * TREE_COLLISION_FRICTION;
        car.yawRate += clamp(vt * 0.045, -0.75, 0.75);
    }
    return true;
}

export function updatePhysics(car, dt, controls, terrain) {
    dt = Math.min(dt, 0.02);
    const throttle = clamp(controls.throttle, -1, 1);
    const steer = clamp(controls.steer, -1, 1);
    const rawThrottle = throttle;
    const wasGrounded = car.grounded;

    const cosH = Math.cos(car.heading);
    const sinH = Math.sin(car.heading);
    let fwdX = cosH, fwdY = sinH;
    let rightX = sinH, rightY = -cosH;

    const support = sampleSupport(car, terrain, fwdX, fwdY, rightX, rightY);
    const speed = Math.hypot(car.vx, car.vy);
    const heightAboveGround = car.z - support.supportZ;
    const canMaintainContact = support.contactCount > 0
        && heightAboveGround <= SUSPENSION_TRAVEL + GROUND_SNAP_EPS
        && (wasGrounded || car.vz <= TAKEOFF_VZ_THRESHOLD);
    const realTakeoff = wasGrounded
        && speed > TAKEOFF_SPEED
        && car.vz > TAKEOFF_VZ_THRESHOLD
        && heightAboveGround > GROUND_SNAP_EPS;

    car.grounded = canMaintainContact && !realTakeoff;
    car.contactCount = car.grounded ? support.contactCount : 0;
    car.compression = car.grounded ? support.compression : 0;

    if (car.grounded) {
        // Fast but smooth suspension constraint. This acts like strong damped
        // springs without explicit high-frequency spring integration.
        car.z = approach(car.z, support.supportZ, SUSPENSION_FOLLOW_RATE, dt);
        if (Math.abs(car.z - support.supportZ) < 0.025) car.z = support.supportZ;
        if (car.vz < 0) car.vz = 0;
        car.airborneTime = 0;
        car.groundZ = support.supportZ;
    } else {
        car.airborneTime += dt;
    }
    const justLeftGround = wasGrounded && !car.grounded;

    const fwdSpeed = car.vx * fwdX + car.vy * fwdY;
    const sideSpeed = car.vx * rightX + car.vy * rightY;
    const absFwdSpeed = Math.abs(fwdSpeed);

    // ----- Tire forces: longitudinal drive + lateral slip correction -----
    const gravityForce = { x: 0, y: 0, z: -physics.gravity * physics.mass };
    let reactionForce = { x: 0, y: 0, z: 0 };
    let gravityTangent = { x: 0, y: 0, z: 0 };
    let tireFx = 0, tireFy = 0;
    let activeThrottle = 0;

    if (car.grounded) {
        const normal = support.normal;
        const dotGN = gravityForce.x * normal.x + gravityForce.y * normal.y + gravityForce.z * normal.z;
        reactionForce = { x: -dotGN * normal.x, y: -dotGN * normal.y, z: -dotGN * normal.z };
        gravityTangent = {
            x: gravityForce.x + reactionForce.x,
            y: gravityForce.y + reactionForce.y,
            z: gravityForce.z + reactionForce.z,
        };

        activeThrottle = throttle;
        const loadScale = clamp(0.35 + support.compression * 0.95, 0.35, 1.3);
        const gripAccel = TIRE_GRIP * loadScale;
        const driveAccel = clamp(activeThrottle * physics.moveForce / Math.max(0.1, physics.mass),
            -gripAccel * LONGITUDINAL_GRIP,
            gripAccel * LONGITUDINAL_GRIP);
        const lateralAccel = clamp(-sideSpeed * LATERAL_STIFFNESS,
            -gripAccel,
            gripAccel);

        tireFx = fwdX * driveAccel + rightX * lateralAccel;
        tireFy = fwdY * driveAccel + rightY * lateralAccel;

        const noDrive = Math.abs(activeThrottle) < 1e-4;
        const rolling = physics.rollingFriction * dt * (noDrive ? 1.0 : 0.35);
        const newFwdSpeed = applyFriction(fwdSpeed, rolling);
        car.vx += fwdX * (newFwdSpeed - fwdSpeed);
        car.vy += fwdY * (newFwdSpeed - fwdSpeed);
    }

    const dragScale = car.grounded ? 0.55 : 0.30;
    const ax = tireFx + (car.grounded ? gravityTangent.x / Math.max(0.1, physics.mass) : 0) - car.vx * physics.linearDrag * dragScale;
    const ay = tireFy + (car.grounded ? gravityTangent.y / Math.max(0.1, physics.mass) : 0) - car.vy * physics.linearDrag * dragScale;
    const az = car.grounded ? 0 : -physics.gravity;

    car.vx += ax * dt;
    car.vy += ay * dt;
    car.vz += az * dt;

    // ----- Steering and yaw inertia -----
    if (car.grounded) {
        const steerAngle = steer * MAX_STEER_ANGLE;
        const lowSpeedBlend = LOW_SPEED_STEER + (1.0 - LOW_SPEED_STEER) * clamp01(absFwdSpeed / 10);
        const speedNorm = fwdSpeed / (absFwdSpeed + 3.0);
        const targetYawRate = -Math.sin(steerAngle) * physics.turnRate * speedNorm * lowSpeedBlend;
        car.yawRate = approach(car.yawRate, targetYawRate, YAW_RESPONSE, dt);
        car.yawRate *= Math.exp(-YAW_DAMPING_GROUND * Math.max(0, Math.abs(sideSpeed) - 0.25) * 0.035 * dt);
    } else {
        car.yawRate *= Math.exp(-YAW_DAMPING_AIR * dt);
    }
    car.heading += car.yawRate * dt;

    if (justLeftGround) {
        const normal = support.normal;
        const slopeForward = normal.x * fwdX + normal.y * fwdY;
        const slopeRight = normal.x * rightX + normal.y * rightY;
        const pitchKick = -slopeForward * speed * 0.22 + rawThrottle * 0.10;
        const rollKick = slopeRight * speed * 0.18 + steer * speed * 0.035;
        car.pitchRate += clamp(pitchKick, -1.2, 1.2);
        car.rollRate += clamp(rollKick, -1.4, 1.4);
    }

    // Rebuild basis after yaw integration for position/vertical terrain follow.
    fwdX = Math.cos(car.heading); fwdY = Math.sin(car.heading);
    rightX = fwdY; rightY = -fwdX;

    const planarSpeed2 = car.vx * car.vx + car.vy * car.vy;
    if (planarSpeed2 > physics.maxSpeed * physics.maxSpeed) {
        const s = physics.maxSpeed / Math.sqrt(planarSpeed2);
        car.vx *= s; car.vy *= s;
    }

    car.x += car.vx * dt;
    car.y += car.vy * dt;

    if (car.grounded) {
        const post = sampleSupport(car, terrain, fwdX, fwdY, rightX, rightY);
        const normal = post.normal;
        car.z = approach(car.z, post.supportZ, SUSPENSION_FOLLOW_RATE, dt);
        if (Math.abs(car.z - post.supportZ) < 0.018) car.z = post.supportZ;
        car.groundZ = post.supportZ;
        car.contactCount = post.contactCount;
        car.compression = post.compression;

        const targetVz = -(normal.x * car.vx + normal.y * car.vy) / Math.max(0.35, normal.z);
        car.vz = approach(car.vz, targetVz, SLOPE_VELOCITY_FOLLOW_RATE, dt);
        // Keep normal terrain ripples planted; only large fast crests become jumps.
        if (car.vz > TAKEOFF_VZ_THRESHOLD && speed < TAKEOFF_SPEED) {
            car.vz = TAKEOFF_VZ_THRESHOLD * 0.45;
        }

        const settle = Math.exp(-BODY_SETTLE_RATE * dt);
        car.pitch *= settle;
        car.roll *= settle;
        car.pitchRate *= Math.exp(-BODY_SETTLE_RATE * 1.2 * dt);
        car.rollRate *= Math.exp(-BODY_SETTLE_RATE * 1.2 * dt);
    } else {
        car.z += car.vz * dt;
        car.pitch += car.pitchRate * dt;
        car.roll += car.rollRate * dt;
        car.pitchRate *= Math.exp(-AIR_ANGULAR_DAMPING * dt);
        car.rollRate *= Math.exp(-AIR_ANGULAR_DAMPING * dt);
        car.pitch = clamp(car.pitch, -1.15, 1.15);
        car.roll = clamp(car.roll, -1.25, 1.25);

        const terrainZ = terrain.getHeight(car.x, car.y);
        if (car.z < terrainZ) {
            const normal = terrain.getNormal(car.x, car.y);
            car.z = terrainZ;
            const vn = car.vx * normal.x + car.vy * normal.y + car.vz * normal.z;
            if (vn < 0) {
                car.vx -= (1 + LANDING_RESTITUTION) * vn * normal.x;
                car.vy -= (1 + LANDING_RESTITUTION) * vn * normal.y;
                car.vz -= (1 + LANDING_RESTITUTION) * vn * normal.z;
            }
            car.grounded = Math.abs(car.vz) < REST_VZ_EPS || vn > -2.8;
            if (car.grounded) {
                car.vz = 0;
                car.airborneTime = 0;
            }
            car.pitchRate *= 0.42;
            car.rollRate *= 0.42;
        }
    }

    const resultantForce = {
        x: fwdX * activeThrottle * physics.moveForce + (car.grounded ? rightX * -sideSpeed * LATERAL_STIFFNESS * physics.mass : 0),
        y: fwdY * activeThrottle * physics.moveForce + (car.grounded ? rightY * -sideSpeed * LATERAL_STIFFNESS * physics.mass : 0),
        z: 0,
    };
    return {
        gravityForce, reactionForce, resultantForce, gravityTangent,
        grounded: car.grounded, rawThrottle
    };
}
