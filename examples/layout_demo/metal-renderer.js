import { createHolograms, METRIC_X, PANELS, placePoint, panelBounds, panelHull, smooth } from './hologram-mesh.js';

const TAU = Math.PI * 2;
const EYE_Y = 17.25;
const EYE_Z = 15.65;
const AIM_Y = 0.51;
const DISTANCE = Math.hypot(EYE_Y - AIM_Y, EYE_Z);
const OVERVIEW_PITCH = Math.atan2(EYE_Y - AIM_Y, EYE_Z);
const RISE = Math.cos(OVERVIEW_PITCH);
const GLOBE_HEIGHT = 2.04;
const GLOBE_RADIUS = 1.27;
const GLOBE_DEPTH = DISTANCE - (GLOBE_HEIGHT - AIM_Y) * Math.sin(OVERVIEW_PITCH);
const FOCUS_PITCH = Math.PI * 0.5 - 0.06;
const ACCENTS = ['#89F4FF', '#8DC5FF', '#CAAAFF', '#FFAEF5'];
const GLYPHS = '01<>/\\#%&$@*+=?ABCDEFXYZ';
const TOUR_DWELL = 4.5;
const ANIMATION_SECONDS = 0.2;
const FADE_SECONDS = 0.16;

const easeInOut = t => t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
const lerp = (a, b, t) => a + (b - a) * t;
const wrapAngle = a => a - TAU * Math.round(a / TAU);

function orbitCamera(pose, out) {
    const cp = Math.cos(pose.pitch), sp = Math.sin(pose.pitch);
    const cy = Math.cos(pose.yaw), sy = Math.sin(pose.yaw);
    out.eye[0] = pose.x + pose.dist * sy * cp;
    out.eye[1] = pose.y + pose.dist * sp;
    out.eye[2] = pose.z + pose.dist * cy * cp;
    out.forward[0] = -sy * cp; out.forward[1] = -sp; out.forward[2] = -cy * cp;
    out.right[0] = cy; out.right[1] = 0; out.right[2] = -sy;
    out.up[0] = -sy * sp; out.up[1] = cp; out.up[2] = -cy * sp;
    out.zoom = pose.zoom;
    return out;
}
const newView = () => ({ eye: [0, 0, 0], right: [1, 0, 0], up: [0, 1, 0], forward: [0, 0, -1], zoom: 1 });
const OVERVIEW = { x: 0, y: AIM_Y, z: 0, yaw: 0, pitch: OVERVIEW_PITCH, dist: DISTANCE, zoom: 1 };
const SKY = { x: 0, y: AIM_Y, z: 0, yaw: Math.PI, pitch: Math.PI * 0.5 - 0.02, dist: DISTANCE * 2.6, zoom: 1 };
const SKY_PANEL = -2;

export function createMetalRenderer() {
    const gl = sys.gl;
    const stone = gl.createProgram('metal-fullscreen.vert', 'metal-background.frag');
    const composite = gl.createProgram('metal-fullscreen.vert', 'metal-copy.frag');
    const energy = gl.createProgram('hologram.vert', 'hologram.frag');
    const globe = gl.createProgram('hologram.vert', 'hologram-globe.frag');
    const mesh = createHolograms(gl, GLOBE_HEIGHT, GLOBE_RADIUS);
    const drawOptions = { mode: 'triangles', count: 0, target: -1, blend: 'alpha',
        depthTest: true, depthWrite: false };
    let scene = -1;
    let previousWidth = 0;
    let previousHeight = 0;
    let screenWidth = 1, screenHeight = 1, baseFocal = 1, focal = 1, aspect = 1;

    const view = newView();
    const overviewView = orbitCamera(OVERVIEW, newView());
    const pose = { ...OVERVIEW };
    const from = { ...OVERVIEW };
    const goal = { ...OVERVIEW };
    const focus = { amounts: new Float64Array(PANELS.length), dim: 0, selected: -1, hover: -1,
        reveal: 1, energy: 0 };
    let progress = 1, duration = 1, spin = 0, swoop = 0, arrived = true;
    let shake = 0, flash = 0, glitch = 0, shockAge = 99, shockX = 0, shockZ = 0;
    let tour = false, tourClock = 0, spinDirection = 1, lastTime = 0, travelLabel = '';
    const projected = { x: 0, y: 0, scale: 0 };
    const point = [0, 0, 0];

    function camera(program, v = view, shift = null) {
        gl.setUniform3f(program, 'u_eye', v.eye[0], v.eye[1], v.eye[2]);
        gl.setUniform3f(program, 'u_right', v.right[0], v.right[1], v.right[2]);
        gl.setUniform3f(program, 'u_up', v.up[0], v.up[1], v.up[2]);
        gl.setUniform3f(program, 'u_forward', v.forward[0], v.forward[1], v.forward[2]);
        gl.setUniform2f(program, 'u_lens', focal, aspect);
        if (shift) gl.setUniform2f(program, 'u_shift', shift[0], shift[1]);
    }

    function project(x, y, z, out = projected) {
        const px = x - view.eye[0], py = y - view.eye[1], pz = z - view.eye[2];
        const depth = px * view.forward[0] + py * view.forward[1] + pz * view.forward[2];
        if (depth < 0.2) return null;
        out.scale = focal * screenHeight / (2 * depth);
        out.x = screenWidth * 0.5 + (px * view.right[0] + pz * view.right[2]) * out.scale;
        out.y = screenHeight * 0.5 - (px * view.up[0] + py * view.up[1] + pz * view.up[2]) * out.scale;
        return out;
    }

    function screenBox(index) {
        const panel = PANELS[index];
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
        for (const p of panelHull(panel)) {
            placePoint(panel, focus.amounts[index], p[0], p[1], p[2], point);
            if (!project(point[0], point[1], point[2])) continue;
            x0 = Math.min(x0, projected.x); x1 = Math.max(x1, projected.x);
            y0 = Math.min(y0, projected.y); y1 = Math.max(y1, projected.y);
        }
        return x1 > x0 ? { x0, y0, x1, y1 } : null;
    }

    function hitTest(x, y) {
        let best = -1, bestArea = Infinity;
        for (let i = 0; i < PANELS.length; i++) {
            const box = screenBox(i);
            if (!box || x < box.x0 - 6 || x > box.x1 + 6 || y < box.y0 - 6 || y > box.y1 + 6) continue;
            const area = (box.x1 - box.x0) * (box.y1 - box.y0);
            if (area < bestArea) { best = i; bestArea = area; }
        }
        return best;
    }

    // Where the pointer ray meets the slab, used as the shockwave origin.
    function groundPoint(x, y) {
        const nx = (2 * x / screenWidth - 1) * aspect / focal;
        const ny = (1 - 2 * y / screenHeight) / focal;
        const dx = view.forward[0] + view.right[0] * nx + view.up[0] * ny;
        const dy = view.forward[1] + view.up[1] * ny;
        const dz = view.forward[2] + view.right[2] * nx + view.up[2] * ny;
        if (dy > -0.01) return null;
        const t = (0.3 - view.eye[1]) / dy;
        return [view.eye[0] + dx * t, view.eye[2] + dz * t];
    }

    function goalPose(index, time, out) {
        if (index === SKY_PANEL) return Object.assign(out, SKY);
        if (index < 0) {
            Object.assign(out, OVERVIEW);
            out.yaw = 0.05 * Math.sin(time * 0.23);
            out.pitch = OVERVIEW_PITCH + 0.02 * Math.sin(time * 0.31);
            return out;
        }
        const panel = PANELS[index];
        const b = panelBounds(panel);
        const halfX = (b.x1 - b.x0) * 0.5, halfZ = (b.z1 - b.z0) * 0.5;
        out.x = (b.x0 + b.x1) * 0.5;
        out.y = b.y;
        out.z = (b.z0 + b.z1) * 0.5 + 0.12;
        out.yaw = 0.025 * Math.sin(time * 0.4);
        out.pitch = FOCUS_PITCH + 0.02 * Math.sin(time * 0.33);
        out.dist = Math.max(halfZ * 1.42, halfX * 1.16 / aspect) * baseFocal + (panel.globe ? GLOBE_HEIGHT + GLOBE_RADIUS : 0);
        out.zoom = 1;
        return out;
    }

    function centerOf(index) {
        const b = panelBounds(PANELS[index]);
        return [(b.x0 + b.x1) * 0.5, b.y, (b.z0 + b.z1) * 0.5];
    }

    function travel(index, time, ground = null) {
        Object.assign(from, pose);
        from.yaw = wrapAngle(from.yaw);
        const leaving = focus.selected;
        focus.selected = index;
        spinDirection = -spinDirection;
        spin = spinDirection * TAU;
        swoop = index === SKY_PANEL || leaving === SKY_PANEL ? 0 : index >= 0 && leaving >= 0 ? 9 : 6;
        duration = ANIMATION_SECONDS;
        travelLabel = index >= 0 ? `WARPING TO ${PANELS[index].name.toUpperCase()}`
            : index === SKY_PANEL ? 'POWERING DOWN HOLOGRAM'
            : leaving === SKY_PANEL ? 'MATERIALIZING HOLOGRAM' : 'RETURNING TO ORBIT';
        progress = 0;
        arrived = false;
        focus.reveal = index >= 0 ? 0 : 1;
        glitch = 1;
        const origin = ground || (index >= 0 ? [centerOf(index)[0], centerOf(index)[2]]
            : leaving >= 0 ? [centerOf(leaving)[0], centerOf(leaving)[2]] : [0, 0]);
        shockX = origin[0]; shockZ = origin[1]; shockAge = 0;
        if (leaving >= 0) {
            const c = centerOf(leaving);
            mesh.burst(c[0], c[1], c[2], 160, 5);
        }
        if (index >= 0) {
            const c = centerOf(index);
            mesh.burst(c[0], 0.3, c[2], 120, 3);
        }
        goalPose(index, time, goal);
    }

    function handleInput(input, time, dt) {
        if (!input) return;
        if (input.back && focus.selected >= 0) { tour = false; travel(-1, time); }
        if (input.space) {
            tour = !tour;
            tourClock = TOUR_DWELL;
        }
        if (input.left || input.right) {
            tour = false;
            const step = input.right ? 1 : PANELS.length - 1;
            travel(((focus.selected < 0 ? (input.right ? -1 : 0) : focus.selected) + step) % PANELS.length, time);
        }
        if (tour && progress >= 1) {
            tourClock += dt;
            if (tourClock >= TOUR_DWELL) {
                tourClock = 0;
                travel((focus.selected + 1) % PANELS.length, time);
            }
        }
        const settled = progress > 0.55;
        focus.hover = focus.selected < 0 && settled ? hitTest(input.x, input.y) : -1;
        if (!input.pressed) return;
        const hit = hitTest(input.x, input.y);
        const ground = groundPoint(input.x, input.y);
        tour = false;
        if (focus.selected < 0) {
            if (hit >= 0) travel(hit, time, ground);
        } else if (hit >= 0 && hit !== focus.selected && PANELS[focus.selected].globe === false) {
            travel(hit, time, ground);
        } else {
            travel(-1, time, ground);
        }
    }

    function animateCamera(time, dt) {
        progress = Math.min(1, progress + dt / duration);
        const e = easeInOut(progress);
        goalPose(focus.selected, time, goal);
        const climb = easeInOut(Math.min(1, progress * (focus.selected >= 0 ? 1.45 : 1.1)));
        const arcLift = Math.sin(Math.PI * e);
        pose.x = lerp(from.x, goal.x, e);
        pose.y = lerp(from.y, goal.y, e);
        pose.z = lerp(from.z, goal.z, e);
        pose.yaw = lerp(from.yaw, goal.yaw + spin, e);
        pose.pitch = lerp(from.pitch, goal.pitch, climb);
        // Dolly zoom: the camera pulls back while the lens narrows, warping perspective mid-flight.
        pose.zoom = 1 + 1.1 * arcLift * arcLift;
        pose.dist = (lerp(from.dist, goal.dist, e) + swoop * arcLift) * pose.zoom;
        if (progress >= 0.6 && !arrived) {
            arrived = true;
            if (focus.selected >= 0) {
                shake = 1;
                flash = 1;
                const c = centerOf(focus.selected);
                mesh.burst(c[0], c[1], c[2], 220, 4.5);
            }
        }
        orbitCamera(pose, view);
        const jolt = shake * shake * 0.11;
        view.eye[0] += jolt * Math.sin(time * 71);
        view.eye[1] += jolt * Math.sin(time * 53 + 1);
        view.eye[2] += jolt * Math.sin(time * 67 + 2);
        focal = baseFocal * pose.zoom;

        const transit = progress < 1 ? Math.sin(Math.PI * progress) : 0;
        if (focus.selected >= 0 && progress > 0.5) focus.reveal = Math.min(1, focus.reveal + dt / (ANIMATION_SECONDS * 0.5));
        for (let i = 0; i < PANELS.length; i++) {
            const target = i === focus.selected && progress > 0.18 ? 1 : 0;
            focus.amounts[i] += Math.max(-dt / FADE_SECONDS, Math.min(dt / FADE_SECONDS, target - focus.amounts[i]));
        }
        const dimTarget = focus.selected >= 0 ? 1 : 0;
        focus.dim += Math.max(-dt / FADE_SECONDS, Math.min(dt / FADE_SECONDS, dimTarget - focus.dim));
        focus.energy = focus.selected >= 0 ? Math.max(transit, 0.16 + 0.12 * Math.sin(time * 9)) * smooth(focus.amounts[focus.selected] * 3) : transit * 0.6;
        shake *= Math.exp(-dt * 45);
        flash *= Math.exp(-dt * 45);
        glitch = Math.max(glitch * Math.exp(-dt * 25), transit * 0.45);
        shockAge += dt;
        return transit;
    }

    function hex(alpha) {
        return Math.round(Math.max(0, Math.min(1, alpha)) * 255).toString(16).padStart(2, '0').toUpperCase();
    }

    function scramble(text, seed) {
        let out = '';
        for (let i = 0; i < text.length; i++) {
            out += text[i] === ' ' ? ' ' : GLYPHS[Math.floor(((Math.sin((i + seed) * 91.7) + 1) * 4371) % GLYPHS.length)];
        }
        return out;
    }

    let labelPanel = -1;
    let labelAlpha = 1;
    let revealZ = Infinity;
    function panelLabels(index, time) {
        labelPanel = index;
        labelAlpha = 1 - 0.9 * focus.dim * (1 - smooth(focus.amounts[index]));
        revealZ = Infinity;
        if (index === focus.selected && focus.reveal < 1) {
            const b = panelBounds(PANELS[index]);
            revealZ = b.z0 + (b.z1 - b.z0) * focus.reveal;
        }
        return Math.floor(time * 20);
    }

    let scrambleSeed = 0;
    function label(text, x, y, z, size, color = '#AAE8FF', align = 'left') {
        if (labelAlpha < 0.02) return;
        placePoint(PANELS[labelPanel], focus.amounts[labelPanel], x, y, z, point);
        if (!project(point[0], point[1], point[2])) return;
        const font = size * projected.scale;
        if (font < 3 || projected.x < -400 || projected.x > screenWidth + 400 || projected.y < -100 || projected.y > screenHeight + 100) return;
        const shown = point[2] > revealZ ? scramble(text, scrambleSeed + Math.floor(x * 10)) : text;
        const offset = align === 'center' ? sys.canvas.measureText(shown, font) * 0.5 : 0;
        sys.canvas.setFillColor('#087CC8' + hex(0.4 * labelAlpha));
        sys.canvas.drawText(shown, projected.x - offset + 1, projected.y + 1, font);
        sys.canvas.setFillColor(color + hex(labelAlpha));
        sys.canvas.drawText(shown, projected.x - offset, projected.y, font);
    }

    function drawLabels(time, { cards, metrics, activities, tasks, regions }) {
        scrambleSeed = panelLabels(4, time);
        label('Performance', -5.25, 2.21, -3.64, 0.20);
        for (const [i, metric] of metrics.entries()) {
            label(metric.display, METRIC_X[i], 0.40 + 2 * metric.value.value, -3.14, 0.18, ACCENTS[i], 'center');
            label(metric.label, METRIC_X[i], 0.24, -2.68, 0.15, ACCENTS[i], 'center');
        }
        scrambleSeed = panelLabels(1, time);
        label('Analytics Chart', 1.43, 2.48, -3.22, 0.20);
        scrambleSeed = panelLabels(0, time);
        for (const [i, card] of cards.entries()) {
            const x = -5.72 + (i % 2) * 2.40;
            const z = 2.20 + Math.floor(i / 2) * 0.87;
            label(card.title, x, 0.26, z, 0.15, ACCENTS[i]);
            label(card.display, x, 0.26, z + 0.28, 0.225, '#E0FBFF');
            label(card.delta, x, 0.24, z + 0.52, 0.13, card.delta[0] === '-' ? '#FFAEF5' : '#89F4FF');
        }
        scrambleSeed = panelLabels(3, time);
        label('Tasks', -5.45, 0.26, -0.68, 0.22, '#E0FBFF');
        for (const [i, task] of tasks.entries()) {
            const z = -0.12 + i * 0.43;
            label(task.done ? '[x]' : '[ ]', -5.44, 0.27, z, 0.16, task.done ? ACCENTS[0] : ACCENTS[2]);
            label(task.text, -5.11, 0.27, z, 0.16);
        }
        scrambleSeed = panelLabels(2, time);
        label('Recent Activity', 3.18, 0.26, -0.68, 0.22, '#E0FBFF');
        for (const [i, activity] of activities.entries()) {
            const z = -0.13 + i * 0.35;
            label(activity.text, 3.18, 0.26, z, 0.155);
            label(activity.time, 5.57, 0.26, z, 0.14, '#8FB3D4');
        }
        scrambleSeed = panelLabels(5, time);
        label('Regional breakdown', 3.85, 0.38, 2.17, 0.17, '#E0FBFF');
        for (const [i, region] of regions.entries()) {
            label(`${region.name}  ${region.display}`, 4.04, 0.37, 2.62 + i * 0.41, 0.15);
        }
    }

    function text(value, x, y, size, color, align = 'left') {
        const offset = align === 'center' ? sys.canvas.measureText(value, size) * 0.5
            : align === 'right' ? sys.canvas.measureText(value, size) : 0;
        sys.canvas.setFillColor(color);
        sys.canvas.drawText(value, x - offset, y, size);
    }

    function drawHud(time, input, transit) {
        const canvas = sys.canvas;
        const small = Math.max(11, Math.min(15, screenHeight / 52));
        const selected = focus.selected;
        canvas.setStrokeWidth(2);
        if (selected >= 0) {
            const amount = smooth(focus.amounts[selected]);
            const box = screenBox(selected);
            if (box && amount > 0.05) {
                const lock = smooth(focus.reveal);
                const inset = (1 - lock) * 60 - 12;
                const x0 = box.x0 - inset, y0 = box.y0 - inset, x1 = box.x1 + inset, y1 = box.y1 + inset;
                const arm = 26 + 10 * (1 - lock);
                const blink = lock < 1 || Math.sin(time * 6) > -0.6;
                canvas.setStrokeColor('#7CF4FF' + hex(amount * (blink ? 1 : 0.4)));
                for (const [cx, cy, sx, sy] of [[x0, y0, 1, 1], [x1, y0, -1, 1], [x1, y1, -1, -1], [x0, y1, 1, -1]]) {
                    canvas.drawLine(cx, cy, cx + sx * arm, cy);
                    canvas.drawLine(cx, cy, cx, cy + sy * arm);
                }
                canvas.setAlpha(amount);
                const status = lock < 1 ? `DECRYPTING ${Math.floor(lock * 100)}%` : 'TARGET LOCKED';
                text(`${status} // ${PANELS[selected].name.toUpperCase()}`, 24, 24 + small, small, '#9CF6FF');
                const altitude = view.eye[1].toFixed(2);
                const heading = Math.round(((pose.yaw * 180 / Math.PI) % 360 + 360) % 360);
                text(`ALT ${altitude}  HDG ${String(heading).padStart(3, '0')}  ZOOM x${(DISTANCE / Math.max(1, pose.dist)).toFixed(2)}`,
                    screenWidth - 30, 84, small * 0.85, '#7FB6FFCC', 'right');
                canvas.setAlpha(1);
            }
        }
        if (transit > 0.02) {
            const w = Math.min(360, screenWidth * 0.4), x = (screenWidth - w) * 0.5, y = screenHeight - 46;
            canvas.setFillColor('#0A1A33AA');
            canvas.drawRect(x, y, w, 6);
            canvas.setFillColor('#7CF4FF');
            canvas.drawRect(x, y, w * progress, 6);
            text(travelLabel, screenWidth * 0.5, y - 10, small, '#BDF8FF' + hex(0.6 + 0.4 * Math.sin(time * 25)), 'center');
        } else {
            const hint = selected >= 0
                ? 'CLICK / BACKSPACE: BACK    LEFT / RIGHT: NEXT PANEL    SPACE: ' + (tour ? 'STOP TOUR' : 'AUTO TOUR')
                : 'CLICK A PANEL TO DIVE IN    SPACE: ' + (tour ? 'STOP TOUR' : 'AUTO TOUR');
            text(hint, screenWidth * 0.5, screenHeight - 24, small * 0.9, '#7FB6FF99', 'center');
        }
        if (focus.hover >= 0 && input) {
            text(`>> ${PANELS[focus.hover].name.toUpperCase()}`, input.x + 18, input.y + 6, small, '#E8FDFF');
        }
        if (tour) text('AUTO TOUR', 24, screenHeight - 24, small, '#FFAEF5' + hex(0.5 + 0.5 * Math.sin(time * 4)));
        if (input) sys.device?.setCursor?.(focus.hover >= 0 ? 'pointer' : 'default');
    }

    function drawGlobe(time, target, v, shift, wipe = null) {
        camera(globe, v, shift);
        if (wipe) gl.setUniform4f(globe, 'u_wipe', wipe.x, wipe.y, wipe.r, screenHeight);
        else gl.setUniform4f(globe, 'u_wipe', 0, 0, 0, 0);
        gl.setUniform1f(globe, 'u_rotation', time * 0.19 * (1 + 4 * focus.energy * (PANELS[focus.selected]?.globe ? 1 : 0)));
        drawOptions.target = target;
        drawOptions.count = mesh.globeCount;
        drawOptions.blend = 'none';
        drawOptions.depthWrite = true;
        gl.drawMesh(globe, mesh.globeLayout, drawOptions);
    }

    function drawClassicGlobe(width, height, time, { x, y, size }, wipe = null) {
        screenHeight = height;
        aspect = width / height;
        focal = size * GLOBE_DEPTH / (height * GLOBE_RADIUS);
        const projectedY = focal * (GLOBE_HEIGHT - AIM_Y) * RISE / GLOBE_DEPTH;
        drawGlobe(time, -1, overviewView, [2 * x / width - 1, 1 - 2 * y / height - projectedY], wipe);
    }

    // Theme switch choreography: dive in from the sky, or climb away while the wipe closes.
    function enterTheme(time) {
        reset();
        Object.assign(pose, SKY);
        focus.selected = SKY_PANEL;
        travel(-1, time, [0, 0]);
    }

    function leaveTheme(time) {
        tour = false;
        travel(SKY_PANEL, time);
    }

    function reset() {
        Object.assign(pose, OVERVIEW);
        focus.selected = -1;
        focus.hover = -1;
        focus.amounts.fill(0);
        focus.dim = 0;
        focus.reveal = 1;
        progress = 1;
        arrived = true;
        tour = false;
    }

    function draw(width, height, time, dashboard, button, drawThemeButton, input = null, wipe = null) {
        const dt = lastTime === 0 ? 1 / 60 : Math.min(0.1, Math.max(0, time - lastTime));
        lastTime = time;
        screenWidth = width;
        screenHeight = height;
        aspect = width / height;
        baseFocal = Math.min(4.62, aspect * 2.67);
        focal = baseFocal * pose.zoom;
        orbitCamera(pose, view);
        handleInput(input, time, dt);
        const transit = animateCamera(time, dt);

        const scale = Math.min(1, 1440 / width, 900 / height);
        const w = Math.max(1, Math.round(width * scale));
        const h = Math.max(1, Math.round(height * scale));
        if (scene < 0) scene = gl.createRenderTarget(w, h, true);
        else if (w !== previousWidth || h !== previousHeight) gl.resizeRenderTarget(scene, w, h);
        previousWidth = w;
        previousHeight = h;
        camera(stone);
        // The shader's shock timeline (~1.5 s) is compressed into ANIMATION_SECONDS.
        gl.setUniform4f(stone, 'u_shock', shockX, shockZ, shockAge * 1.5 / ANIMATION_SECONDS,
            Math.max(0, 1 - shockAge / ANIMATION_SECONDS));
        gl.drawRegion(stone, 0, 0, w, h, scene);
        const shift = [0, 0];
        drawGlobe(time, scene, view, shift);

        mesh.update(time, dt, dashboard, view, focus);
        camera(energy, view, shift);
        drawOptions.count = mesh.count;
        drawOptions.blend = 'alpha';
        drawOptions.depthWrite = false;
        gl.drawMesh(energy, mesh.layout, drawOptions);

        if (wipe) {
            sys.canvas.save();
            sys.canvas.clipRoundRect(wipe.x - wipe.r, wipe.y - wipe.r, wipe.r * 2, wipe.r * 2, wipe.r);
        } else {
            sys.canvas.clear('#00000000');
        }
        drawLabels(time, dashboard);
        drawHud(time, input, transit);
        drawThemeButton(button);
        if (wipe) {
            sys.canvas.restore();
            drawWipeEdge(wipe, time);
        }

        gl.bindScreen();
        gl.bindTexture(composite, 'u_scene', scene, 1);
        gl.setUniform2f(composite, 'u_texel', 1 / w, 1 / h);
        const tear = wipe ? Math.sin(Math.PI * Math.min(1, wipe.r / wipe.max)) : 0;
        gl.setUniform4f(composite, 'u_fx', 0.35 + 2.6 * transit + flash + 2 * tear, flash, Math.max(glitch, tear * 0.8), time);
        gl.drawFullscreen(composite);
    }

    function drawWipeEdge({ x, y, r }, time) {
        const canvas = sys.canvas;
        for (let i = 4; i >= 0; i--) {
            canvas.setStrokeWidth(2 + i * 5);
            canvas.setStrokeColor(i ? '#3FA8FF' + hex(0.10 + 0.04 * (4 - i)) : '#D8FBFF');
            canvas.drawArc(x - r, y - r, r * 2, r * 2, 0, 360, false);
        }
        canvas.setStrokeWidth(2);
        for (let i = 0; i < 24; i++) {
            const a = TAU * i / 24 + time * 1.7;
            const inner = r - 6 - (i % 3) * 10, outer = r + 14 + (i % 4) * 8;
            canvas.setStrokeColor(i % 2 ? '#FFAEF5AA' : '#89F4FFCC');
            canvas.drawLine(x + Math.cos(a) * inner, y + Math.sin(a) * inner, x + Math.cos(a) * outer, y + Math.sin(a) * outer);
        }
    }

    return { draw, drawClassicGlobe, reset, enterTheme, leaveTheme };
}
