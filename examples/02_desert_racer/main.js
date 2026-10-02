/**
 * Terrain Physics Demo - Budo
 *
 * Uses the sys.gl 3D mesh pipeline:
 *   - Terrain   : a real triangulated mesh with per-vertex normals (Lambert lit,
 *                 height-based color, distance fog).
 *   - Car       : `Mesh.cube`, transformed by a model matrix that aligns its
 *                 local Z to the terrain normal and local Y to the heading.
 *   - Force vis : a dynamic line-mode mesh updated each frame.
 *   - HUD/joystick/buttons stay in 2D Skia (drawn before the GL pass; they
 *     are visible wherever the terrain mesh does not cover them, e.g. sky).
 *
 * Controls:
 *   Desktop:
 *     Car movement: Arrow keys or WASD
 *     Camera rotation (around Z): Q / E
 *     Camera angle: R / F
 *     Camera distance: T / G
 *   Touch (Android):
 *     Left side: Virtual joystick for car movement
 *     Right side: Camera control buttons
 */

class ShaderProgram {
    programId

    constructor(programId) {
        this.programId = programId
        if (this.programId < 0) throw "invalid program id"
    }

    uniform1i(name, value) {
        sys.gl.setUniform1i(this.programId, name, value)
        return this
    }

    uniform1f(name, value) {
        sys.gl.setUniform1f(this.programId, name, value)
        return this
    }

    uniform2f(name, value0, value1) {
        sys.gl.setUniform2f(this.programId, name, value0, value1)
        return this
    }

    uniform3f(name, v0, v1, v2) {
        sys.gl.setUniform3f(this.programId, name, v0, v1, v2)
        return this
    }

    uniform4f(name, v0, v1, v2, v3) {
        sys.gl.setUniform4f(this.programId, name, v0, v1, v2, v3)
        return this
    }

    uniformMatrix4(name, mat16) {
        sys.gl.setUniformMatrix4(this.programId, name, mat16)
        return this
    }

    // in the original c wrapper program.drawFullscreen was calling gl.drawFullscreenImmediate
    drawFullscreen() {
        sys.gl.drawFullscreenImmediate(this.programId)
        return this
    }

    drawRegion(x, y, w, h, targetId) {
        sys.gl.drawRegionImmediate(this.programId, x, y, w, h, targetId)
        return this
    }

    // in the original c wrapper program.drawMesh was calling gl.drawMesgImmediate
    drawMesh(layoutId, options) {
        sys.gl.drawMeshImmediate(this.programId, layoutId, options)
    }
}

function createShaderProgram(vert, frag) {
    return new ShaderProgram(sys.gl.createProgram(vert, frag))
}

import { Mesh } from './mesh.js';
import {
    CAR_HEIGHT,
    CAR_LENGTH,
    CAR_WIDTH,
    adjustParam,
    gatherAIControls,
    makeCar,
    physics,
    physicsParams,
    pickAIControls,
    resetPhysics,
    resolveTreeCollision,
    resolveVehicleCollisions,
    updatePhysics,
} from './car_physics.js';

let screenWidth = sys.window.getWidth();
let screenHeight = sys.window.getHeight();
const DISPLAY_DENSITY = sys.window.getDisplayDensity();
const MOBILE_PROFILE = DISPLAY_DENSITY > 1.25;
sys.log(`Terrain profile: ${MOBILE_PROFILE ? 'mobile' : 'desktop'} density=${DISPLAY_DENSITY.toFixed(2)} size=${screenWidth}x${screenHeight}`);

// ============ Touch / Virtual Joystick State ============

let JOYSTICK_RADIUS = 0;
let JOYSTICK_X = 0;
let JOYSTICK_Y = 0;
const JOYSTICK_DEAD_ZONE = 0.15;

let touchJoystickActive = false;
let touchJoystickX = 0;
let touchJoystickY = 0;

const BUTTON_SIZE = 60;
let BUTTON_X = 0;
let BUTTON_Y_START = 0;

const cameraButtons = [
    { x: 0, y: 0, label: '◀', action: 'rotLeft' },
    { x: 0, y: 0, label: '▶', action: 'rotRight' },
    { x: 0, y: 0, label: '▲', action: 'angleUp' },
    { x: 0, y: 0, label: '▼', action: 'angleDown' },
    { x: 0, y: 0, label: '+', action: 'zoomIn' },
    { x: 0, y: 0, label: '-', action: 'zoomOut' },
];

// View toggle button (third-person <-> first-person). Lives above the camera
// rotate cluster on the right edge of the screen.
let VIEW_BTN_X = 0;
let VIEW_BTN_Y = 0;
const VIEW_BTN_W = 112;
const VIEW_BTN_H = 36;
let TARGET_BTN_X = 0;
let TARGET_BTN_Y = 0;
const TARGET_BTN_W = 112;
const TARGET_BTN_H = 36;

let activeButtons = new Set();

const VIEW_ORBIT = 0;
const VIEW_FOLLOW = 1;
const VIEW_FPV = 2;

// View mode: orbit = original camera, follow = original camera with slow
// heading-follow, FPV = first-person cabin camera.
let viewMode = VIEW_FOLLOW;
let showRallyTarget = false;

function isFirstPersonView() { return viewMode === VIEW_FPV; }
function cycleViewMode() { viewMode = (viewMode + 1) % 3; }
function viewModeLabel() {
    return viewMode === VIEW_ORBIT ? 'ORBIT'
        : viewMode === VIEW_FOLLOW ? 'FOLLOW'
            : 'FPV';
}

// ============ Physics-panel layout ============

const PARAM_PANEL_WIDTH = 230;
let PARAM_PANEL_X = 0;
const PARAM_PANEL_Y = 170; // below the force legend
const PARAM_ROW_HEIGHT = 22;
const PARAM_BTN_SIZE = 18;
// Total panel height including title, rows and reset button.
const PARAM_PANEL_HEIGHT = 30 + 8 * PARAM_ROW_HEIGHT + 30;

function updateResponsiveLayout() {
    screenWidth = sys.window.getWidth();
    screenHeight = sys.window.getHeight();

    JOYSTICK_RADIUS = Math.min(screenWidth, screenHeight) * 0.12;
    JOYSTICK_X = JOYSTICK_RADIUS + 40;
    JOYSTICK_Y = screenHeight - JOYSTICK_RADIUS - 80;

    BUTTON_X = screenWidth - BUTTON_SIZE - 30;
    BUTTON_Y_START = screenHeight - 280;
    cameraButtons[0].x = BUTTON_X - 40; cameraButtons[0].y = BUTTON_Y_START;
    cameraButtons[1].x = BUTTON_X + 40; cameraButtons[1].y = BUTTON_Y_START;
    cameraButtons[2].x = BUTTON_X; cameraButtons[2].y = BUTTON_Y_START + 70;
    cameraButtons[3].x = BUTTON_X; cameraButtons[3].y = BUTTON_Y_START + 140;
    cameraButtons[4].x = BUTTON_X - 40; cameraButtons[4].y = BUTTON_Y_START + 210;
    cameraButtons[5].x = BUTTON_X + 40; cameraButtons[5].y = BUTTON_Y_START + 210;

    VIEW_BTN_X = BUTTON_X;
    VIEW_BTN_Y = BUTTON_Y_START - 80;
    TARGET_BTN_X = BUTTON_X;
    TARGET_BTN_Y = BUTTON_Y_START - 124;
    PARAM_PANEL_X = screenWidth - PARAM_PANEL_WIDTH - 10;
}
updateResponsiveLayout();

// ============ Deterministic value noise ============
//
// Procedural, seedable, allocation-free, and considerably faster than the
// gradient-Perlin + permutation-table approach previously used here. The
// terrain is now defined for every (x, y) in R^2 by a pure function of its
// inputs, which is what makes the world infinite and reproducible across
// runs / machines / chunk boundaries.
//
// hash2(): 32-bit integer mix of two integer coordinates -> [0, 1).
// All math stays in 32-bit int land (Math.imul / >>> 0) so it is fast and
// deterministic regardless of float behaviour.

const NOISE_SEED = 0x9E3779B1 | 0; // golden-ratio constant; change to reshape world

function hash2(ix, iy) {
    let h = (Math.imul(ix | 0, 0x27D4EB2D) ^
        Math.imul(iy | 0, 0x165667B1) ^
        NOISE_SEED) | 0;
    h = Math.imul(h ^ (h >>> 15), 0x85EBCA6B);
    h = Math.imul(h ^ (h >>> 13), 0xC2B2AE35);
    h ^= h >>> 16;
    return (h >>> 0) / 4294967296;
}
function valueNoise(x, y) {
    const ix = Math.floor(x), iy = Math.floor(y);
    const fx = x - ix, fy = y - iy;
    let h = (Math.imul(ix | 0, 0x27D4EB2D) ^
        Math.imul(iy | 0, 0x165667B1) ^ NOISE_SEED) | 0;
    h = Math.imul(h ^ (h >>> 15), 0x85EBCA6B);
    h = Math.imul(h ^ (h >>> 13), 0xC2B2AE35);
    h ^= h >>> 16;
    const a = (h >>> 0) / 4294967296;

    h = (Math.imul((ix + 1) | 0, 0x27D4EB2D) ^
        Math.imul(iy | 0, 0x165667B1) ^ NOISE_SEED) | 0;
    h = Math.imul(h ^ (h >>> 15), 0x85EBCA6B);
    h = Math.imul(h ^ (h >>> 13), 0xC2B2AE35);
    h ^= h >>> 16;
    const b = (h >>> 0) / 4294967296;

    h = (Math.imul(ix | 0, 0x27D4EB2D) ^
        Math.imul((iy + 1) | 0, 0x165667B1) ^ NOISE_SEED) | 0;
    h = Math.imul(h ^ (h >>> 15), 0x85EBCA6B);
    h = Math.imul(h ^ (h >>> 13), 0xC2B2AE35);
    h ^= h >>> 16;
    const c = (h >>> 0) / 4294967296;

    h = (Math.imul((ix + 1) | 0, 0x27D4EB2D) ^
        Math.imul((iy + 1) | 0, 0x165667B1) ^ NOISE_SEED) | 0;
    h = Math.imul(h ^ (h >>> 15), 0x85EBCA6B);
    h = Math.imul(h ^ (h >>> 13), 0xC2B2AE35);
    h ^= h >>> 16;
    const d = (h >>> 0) / 4294967296;

    const u = fx * fx * fx * (fx * (fx * 6 - 15) + 10);
    const v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    const ab = a + u * (b - a);
    const cd = c + u * (d - c);
    // Map [0,1) -> [-1,1] so fbm sums symmetrically.
    return (ab + v * (cd - ab)) * 2 - 1;
}

function fbm(x, y, octaves, persistence) {
    let total = 0, amplitude = 1, frequency = 1, maxValue = 0;
    for (let i = 0; i < octaves; i++) {
        total += valueNoise(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2;
    }
    return total / maxValue;
}

// ============ Infinite chunked terrain ============
//
// The world is tiled by square chunks of CHUNK_CELLS x CHUNK_CELLS quads.
// Only chunks within VIEW_RADIUS_CHUNKS of the car are kept on the GPU;
// chunks moving out of range are destroyed and their slots reused.
//
// Heights are a pure function fbm(x*NOISE_SCALE, y*NOISE_SCALE) so chunks
// share boundary vertices exactly, with no seams.

const TERRAIN_SCALE = MOBILE_PROFILE ? 2.0 : 1.0; // World unit per cell
const TERRAIN_HEIGHT_SCALE = 3.2;
const NOISE_SCALE = 0.055;
const NOISE_OCTAVES = MOBILE_PROFILE ? 3 : 4;
const NOISE_PERSISTENCE = 0.45;
const TERRAIN_AMP_1 = NOISE_PERSISTENCE;
const TERRAIN_AMP_2 = TERRAIN_AMP_1 * NOISE_PERSISTENCE;
const TERRAIN_AMP_3 = TERRAIN_AMP_2 * NOISE_PERSISTENCE;
const TERRAIN_FBM_MAX_3 = 1 + TERRAIN_AMP_1 + TERRAIN_AMP_2;
const TERRAIN_FBM_MAX_4 = TERRAIN_FBM_MAX_3 + TERRAIN_AMP_3;

const CHUNK_CELLS = MOBILE_PROFILE ? 12 : 32; // quads per chunk side
const CHUNK_WORLD = CHUNK_CELLS * TERRAIN_SCALE;
const CHUNK_VERTS_SIDE = CHUNK_CELLS + 1;
const VIEW_RADIUS_CHUNKS = MOBILE_PROFILE ? 1 : 2; // 9 chunks on Android/tablets, 25 on desktop
const VIEW_KEEP_CHUNKS = VIEW_RADIUS_CHUNKS + 1; // hysteresis to avoid thrash
const PREFETCH_RADIUS_CHUNKS = MOBILE_PROFILE ? VIEW_KEEP_CHUNKS : VIEW_RADIUS_CHUNKS;
const CHUNK_BUILD_BUDGET = MOBILE_PROFILE ? 1 : 64; // avoid frame spikes when crossing chunk edges
const CHUNK_DESTROY_BUDGET = MOBILE_PROFILE ? 1 : 64;
const CHUNK_INITIAL_BUDGET = MOBILE_PROFILE ? 9 : 64; // build the spawn view immediately

// Pure terrain field. Hot path: called by physics every frame and by every
// vertex of every newly-built chunk. This is the fixed-parameter equivalent
// of fbm(..., NOISE_OCTAVES, NOISE_PERSISTENCE), unrolled because shadows and
// headlights call it hundreds of times per frame in the desktop profile.
function getTerrainHeight(x, y) {
    const nx = x * NOISE_SCALE;
    const ny = y * NOISE_SCALE;
    let total = valueNoise(nx, ny);
    total += valueNoise(nx * 2, ny * 2) * TERRAIN_AMP_1;
    total += valueNoise(nx * 4, ny * 4) * TERRAIN_AMP_2;
    if (NOISE_OCTAVES === 3) {
        return total / TERRAIN_FBM_MAX_3 * TERRAIN_HEIGHT_SCALE;
    }
    total += valueNoise(nx * 8, ny * 8) * TERRAIN_AMP_3;
    return total / TERRAIN_FBM_MAX_4 * TERRAIN_HEIGHT_SCALE;
}

// Reusable scratch object so getTerrainNormal does not allocate per call.
const _normalScratch = { x: 0, y: 0, z: 1 };
const NORMAL_EPS = 0.9; // central-difference step in world units
function getTerrainNormal(x, y) {
    const hL = getTerrainHeight(x - NORMAL_EPS, y);
    const hR = getTerrainHeight(x + NORMAL_EPS, y);
    const hD = getTerrainHeight(x, y - NORMAL_EPS);
    const hU = getTerrainHeight(x, y + NORMAL_EPS);
    const nx = (hL - hR) / (2 * NORMAL_EPS);
    const ny = (hD - hU) / (2 * NORMAL_EPS);
    const nz = 1.0;
    const len = Math.sqrt(nx * nx + ny * ny + nz * nz);
    _normalScratch.x = nx / len;
    _normalScratch.y = ny / len;
    _normalScratch.z = nz / len;
    return _normalScratch;
}

// Indices are identical for every chunk: build once, share GPU buffer.
const CHUNK_INDEX_COUNT = CHUNK_CELLS * CHUNK_CELLS * 6;
const chunkIndexData = new Uint16Array(CHUNK_INDEX_COUNT);
{
    let ii = 0;
    const W = CHUNK_VERTS_SIDE;
    for (let y = 0; y < CHUNK_CELLS; y++) {
        for (let x = 0; x < CHUNK_CELLS; x++) {
            const a = y * W + x;
            const b = a + 1;
            const c = a + W;
            const d = c + 1;
            chunkIndexData[ii++] = a; chunkIndexData[ii++] = b; chunkIndexData[ii++] = d;
            chunkIndexData[ii++] = a; chunkIndexData[ii++] = d; chunkIndexData[ii++] = c;
        }
    }
}
const sharedIndexBuffer = sys.gl.createBuffer('index', chunkIndexData);

// Scratch typed arrays reused across chunk builds (no per-chunk GC).
const _chunkPositions = new Float32Array(CHUNK_VERTS_SIDE * CHUNK_VERTS_SIDE * 3);
const _chunkNormals = new Float32Array(CHUNK_VERTS_SIDE * CHUNK_VERTS_SIDE * 3);
// Heights for the chunk plus a 1-cell apron on every side, used to compute
// normals via central differences without re-evaluating fbm at boundaries.
const _chunkHeights = new Float32Array((CHUNK_VERTS_SIDE + 2) * (CHUNK_VERTS_SIDE + 2));

function buildChunkMesh(cx, cy) {
    const baseX = cx * CHUNK_WORLD;
    const baseY = cy * CHUNK_WORLD;
    const W = CHUNK_VERTS_SIDE;
    const HW = W + 2; // padded heights row stride

    // 1) Sample heights for the padded grid (one extra ring around the chunk).
    for (let j = 0; j < HW; j++) {
        const wy = baseY + (j - 1) * TERRAIN_SCALE;
        const row = j * HW;
        for (let i = 0; i < HW; i++) {
            const wx = baseX + (i - 1) * TERRAIN_SCALE;
            _chunkHeights[row + i] = getTerrainHeight(wx, wy);
        }
    }

    // 2) Emit positions + central-difference normals for the inner WxW grid.
    const dxy = 2 * TERRAIN_SCALE;
    let pi = 0, ni = 0;
    for (let j = 0; j < W; j++) {
        const wy = baseY + j * TERRAIN_SCALE;
        const padRow = (j + 1) * HW;
        for (let i = 0; i < W; i++) {
            const wx = baseX + i * TERRAIN_SCALE;
            const h = _chunkHeights[padRow + (i + 1)];
            _chunkPositions[pi++] = wx;
            _chunkPositions[pi++] = wy;
            _chunkPositions[pi++] = h;

            const hL = _chunkHeights[padRow + i];
            const hR = _chunkHeights[padRow + (i + 2)];
            const hD = _chunkHeights[padRow - HW + (i + 1)];
            const hU = _chunkHeights[padRow + HW + (i + 1)];
            _chunkNormals[ni++] = (hL - hR) / dxy;
            _chunkNormals[ni++] = (hD - hU) / dxy;
            _chunkNormals[ni++] = 1.0;
        }
    }
    sys.math.vec3NormalizeMany(_chunkNormals, _chunkNormals, W * W);
}

function createChunkGPU(cx, cy) {
    buildChunkMesh(cx, cy);
    const posBuf = sys.gl.createBuffer('vertex', _chunkPositions);
    const nrmBuf = sys.gl.createBuffer('vertex', _chunkNormals);
    const layout = sys.gl.createVertexLayout();
    sys.gl.setAttribute(layout, 0 /* a_position */, posBuf, 3, 'float', false, 0, 0);
    sys.gl.setAttribute(layout, 2 /* a_normal   */, nrmBuf, 3, 'float', false, 0, 0);
    sys.gl.setIndexBuffer(layout, sharedIndexBuffer, 'u16');
    return { cx, cy, layout, posBuf, nrmBuf };
}

function destroyChunkGPU(chunk) {
    sys.gl.destroyVertexLayout(chunk.layout);
    sys.gl.destroyBuffer(chunk.posBuf);
    sys.gl.destroyBuffer(chunk.nrmBuf);
}

// Active chunks keyed by "cx,cy".
const activeChunks = new Map();
const chunkBuildQueue = [];
const chunkDestroyQueue = [];
const pendingChunkKeys = new Set();
let currentChunkX = 0;
let currentChunkY = 0;

function chunkKey(cx, cy) { return cx + ',' + cy; }

function isChunkVisible(chunk) {
    return Math.abs(chunk.cx - currentChunkX) <= VIEW_RADIUS_CHUNKS
        && Math.abs(chunk.cy - currentChunkY) <= VIEW_RADIUS_CHUNKS;
}

function updateChunks(carWorldX, carWorldY) {
    const ccx = Math.floor(carWorldX / CHUNK_WORLD);
    const ccy = Math.floor(carWorldY / CHUNK_WORLD);
    currentChunkX = ccx;
    currentChunkY = ccy;

    // Evict chunks before allocating new ones. This keeps GPU buffer usage
    // bounded even after long drives or fast teleports across chunk rings.
    for (const [k, chunk] of activeChunks) {
        if (Math.abs(chunk.cx - ccx) > VIEW_KEEP_CHUNKS ||
            Math.abs(chunk.cy - ccy) > VIEW_KEEP_CHUNKS) {
            activeChunks.delete(k);
            chunkDestroyQueue.push(chunk);
        }
    }

    for (let destroyed = 0; destroyed < CHUNK_DESTROY_BUDGET && chunkDestroyQueue.length > 0; destroyed++) {
        destroyChunkGPU(chunkDestroyQueue.shift());
    }

    // Queue missing chunks beyond the visible ring, then build only a small
    // budget per frame. On Android, building a visible chunk at the exact
    // boundary crossing creates a hitch, so keep a prefetched cache around the
    // 3x3 visible set and only draw the visible subset.
    const missing = [];
    for (let dy = -PREFETCH_RADIUS_CHUNKS; dy <= PREFETCH_RADIUS_CHUNKS; dy++) {
        for (let dx = -PREFETCH_RADIUS_CHUNKS; dx <= PREFETCH_RADIUS_CHUNKS; dx++) {
            const cx = ccx + dx, cy = ccy + dy;
            const k = chunkKey(cx, cy);
            if (!activeChunks.has(k) && !pendingChunkKeys.has(k)) {
                missing.push({ cx, cy, k, dist2: dx * dx + dy * dy });
            }
        }
    }
    missing.sort((a, b) => a.dist2 - b.dist2);
    for (const item of missing) {
        pendingChunkKeys.add(item.k);
        chunkBuildQueue.push(item);
    }
    chunkBuildQueue.sort((a, b) => {
        const adx = a.cx - ccx, ady = a.cy - ccy;
        const bdx = b.cx - ccx, bdy = b.cy - ccy;
        return (adx * adx + ady * ady) - (bdx * bdx + bdy * bdy);
    });

    const buildBudget = activeChunks.size === 0 ? CHUNK_INITIAL_BUDGET : CHUNK_BUILD_BUDGET;
    for (let built = 0; built < buildBudget && chunkBuildQueue.length > 0;) {
        const item = chunkBuildQueue.shift();
        pendingChunkKeys.delete(item.k);
        if (activeChunks.has(item.k)) continue;
        if (Math.abs(item.cx - ccx) > PREFETCH_RADIUS_CHUNKS ||
            Math.abs(item.cy - ccy) > PREFETCH_RADIUS_CHUNKS) {
            continue;
        }
        activeChunks.set(item.k, createChunkGPU(item.cx, item.cy));
        built++;
    }
}

const terrainProgram = createShaderProgram('terrain.vert', 'terrain.frag');

// ============ Desert vegetation: deterministic low-poly palm trees ============
// Palms are not individual GPU meshes. A few shared variants are uploaded once,
// then randomly placed per terrain chunk from deterministic chunk seeds. Each
// visible tree is one indexed draw call with vertex colors baked into the mesh.

const PALM_DENSITY = 0.58;
const PALM_MAX_PER_CHUNK = 2;
const PALM_MIN_PLAYER_DIST = 7.0;
const PALM_DRAW_RADIUS = MOBILE_PROFILE ? 36.0 : 78.0;
const PALM_MAX_DRAWS = MOBILE_PROFILE ? 3 : 18;
const PALM_COLLISION_RADIUS = 0.15;
const palmDrawList = [];

function palmRand(seed, salt) {
    return hash2((seed * 73856093 + salt * 19349663) | 0,
        (seed * 83492791 + salt * 2971215073) | 0);
}

function pushPalmVertex(positions, normals, colors, p, n, c) {
    positions.push(p[0], p[1], p[2]);
    normals.push(n[0], n[1], n[2]);
    colors.push(c[0], c[1], c[2]);
    return (positions.length / 3) - 1;
}

function palmNormal(a, b, c) {
    const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    const nx = uy * vz - uz * vy;
    const ny = uz * vx - ux * vz;
    const nz = ux * vy - uy * vx;
    const len = Math.sqrt(nx * nx + ny * ny + nz * nz) || 1;
    return [nx / len, ny / len, nz / len];
}

function addPalmTri(positions, normals, colors, indices, a, b, c, color) {
    const n = palmNormal(a, b, c);
    const base = positions.length / 3;
    pushPalmVertex(positions, normals, colors, a, n, color);
    pushPalmVertex(positions, normals, colors, b, n, color);
    pushPalmVertex(positions, normals, colors, c, n, color);
    indices.push(base, base + 1, base + 2);
}

function addPalmQuad(positions, normals, colors, indices, a, b, c, d, color) {
    addPalmTri(positions, normals, colors, indices, a, b, c, color);
    addPalmTri(positions, normals, colors, indices, a, c, d, color);
}

function makePalmMesh(variant) {
    const positions = [], normals = [], colors = [], indices = [];
    const trunkColorA = [0.43, 0.25, 0.11];
    const trunkColorB = [0.62, 0.39, 0.19];
    const leafColorA = [0.10, 0.42, 0.18];
    const leafColorB = [0.30, 0.58, 0.20];
    const dryLeaf = [0.63, 0.54, 0.27];

    const height = 4.7 + variant * 0.45;
    const radial = 7;
    const rings = 6;
    const leanAngle = variant * 1.9;
    const leanX = Math.cos(leanAngle) * (0.18 + variant * 0.05);
    const leanY = Math.sin(leanAngle) * (0.18 + variant * 0.05);

    // Tapered, faceted trunk with slight lean and alternating warm bands.
    for (let r = 0; r <= rings; r++) {
        const t = r / rings;
        const z = t * height;
        const cx = leanX * t * t;
        const cy = leanY * t * t;
        const radius = 0.24 * (1.0 - t * 0.38);
        for (let s = 0; s < radial; s++) {
            const a = (s / radial) * Math.PI * 2;
            const ca = Math.cos(a), sa = Math.sin(a);
            positions.push(cx + ca * radius, cy + sa * radius, z);
            normals.push(ca, sa, 0.18);
            const band = ((r + s) & 1) ? trunkColorA : trunkColorB;
            colors.push(band[0], band[1], band[2]);
        }
    }
    for (let r = 0; r < rings; r++) {
        for (let s = 0; s < radial; s++) {
            const a = r * radial + s;
            const b = r * radial + ((s + 1) % radial);
            const c = (r + 1) * radial + ((s + 1) % radial);
            const d = (r + 1) * radial + s;
            indices.push(a, b, c, a, c, d);
        }
    }

    const topX = leanX;
    const topY = leanY;
    const topZ = height;
    const fronds = 9 + variant;
    for (let i = 0; i < fronds; i++) {
        const a = (i / fronds) * Math.PI * 2 + variant * 0.31;
        const ca = Math.cos(a), sa = Math.sin(a);
        const sideX = -sa, sideY = ca;
        const length = 2.25 + palmRand(variant + 11, i) * 0.75;
        const width = 0.34 + palmRand(variant + 17, i) * 0.12;
        const droop = 0.62 + palmRand(variant + 23, i) * 0.35;
        const lift = 0.18 + palmRand(variant + 29, i) * 0.22;
        const col = (i % 5 === 0) ? dryLeaf : ((i & 1) ? leafColorA : leafColorB);

        const base = [topX + ca * 0.12, topY + sa * 0.12, topZ + 0.08];
        const midL = [topX + ca * length * 0.55 + sideX * width,
        topY + sa * length * 0.55 + sideY * width,
        topZ + lift - droop * 0.28];
        const midR = [topX + ca * length * 0.55 - sideX * width,
        topY + sa * length * 0.55 - sideY * width,
        topZ + lift - droop * 0.28];
        const tip = [topX + ca * length,
        topY + sa * length,
        topZ - droop];
        addPalmTri(positions, normals, colors, indices, base, midL, midR, col);
        addPalmTri(positions, normals, colors, indices, midL, tip, midR, col);
    }

    return {
        positions: new Float32Array(positions),
        normals: new Float32Array(normals),
        colors: new Float32Array(colors),
        indices: new Uint16Array(indices),
    };
}

function uploadPalmMesh(mesh) {
    const posBuf = sys.gl.createBuffer('vertex', mesh.positions);
    const nrmBuf = sys.gl.createBuffer('vertex', mesh.normals);
    const colorBuf = sys.gl.createBuffer('vertex', mesh.colors);
    const idxBuf = sys.gl.createBuffer('index', mesh.indices);
    const layout = sys.gl.createVertexLayout();
    sys.gl.setAttribute(layout, 0 /* a_position */, posBuf, 3, 'float', false, 0, 0);
    sys.gl.setAttribute(layout, 2 /* a_normal   */, nrmBuf, 3, 'float', false, 0, 0);
    sys.gl.setAttribute(layout, 3 /* a_color    */, colorBuf, 3, 'float', false, 0, 0);
    sys.gl.setIndexBuffer(layout, idxBuf, 'u16');
    return { layout, indexCount: mesh.indices.length, buffers: { posBuf, nrmBuf, colorBuf, idxBuf } };
}

const palmProgram = createShaderProgram('palm.vert', 'palm.frag');
const palmVariants = [
    uploadPalmMesh(makePalmMesh(0)),
    uploadPalmMesh(makePalmMesh(1)),
    uploadPalmMesh(makePalmMesh(2)),
];
const palmChunks = new Map();

function getPalmChunk(cx, cy) {
    const k = chunkKey(cx, cy);
    let palms = palmChunks.get(k);
    if (palms) return palms;

    palms = [];
    const seed = (Math.imul(cx | 0, 92837111) ^ Math.imul(cy | 0, 689287499)) | 0;
    if (palmRand(seed, 0) < PALM_DENSITY) {
        const count = 1 + Math.floor(palmRand(seed, 1) * PALM_MAX_PER_CHUNK);
        for (let i = 0; i < count; i++) {
            const x = cx * CHUNK_WORLD + palmRand(seed, 10 + i * 8) * CHUNK_WORLD;
            const y = cy * CHUNK_WORLD + palmRand(seed, 11 + i * 8) * CHUNK_WORLD;
            if (Math.hypot(x - playerCar.x, y - playerCar.y) < PALM_MIN_PLAYER_DIST) continue;

            const normal = getTerrainNormal(x, y);
            if (normal.z < 0.78) continue;

            palms.push({
                x,
                y,
                z: getTerrainHeight(x, y),
                rot: palmRand(seed, 12 + i * 8) * Math.PI * 2,
                scale: 0.82 + palmRand(seed, 13 + i * 8) * 0.44,
                variant: Math.floor(palmRand(seed, 14 + i * 8) * palmVariants.length),
            });
        }
    }
    palmChunks.set(k, palms);
    return palms;
}

function prunePalmChunks() {
    for (const k of palmChunks.keys()) {
        if (!activeChunks.has(k)) palmChunks.delete(k);
    }
}

function resolvePalmCollisions() {
    for (const chunk of activeChunks.values()) {
        if (!isChunkVisible(chunk)) continue;
        const palms = getPalmChunk(chunk.cx, chunk.cy);
        for (const palm of palms) {
            const radius = PALM_COLLISION_RADIUS * palm.scale;
            for (const car of cars) {
                resolveTreeCollision(car, palm.x, palm.y, radius);
            }
        }
    }
}

// ============ Build car mesh once ============
//
// Uniforms are applied immediately to the shared car program before each draw.
// This lets all cars reuse one shader while still giving each mesh its own
// model matrix and material state.
const KENNEY_CAR_ASSETS = [
    'assets/kenney_car_kit/ambulance.obj',
    'assets/kenney_car_kit/delivery-flat.obj',
    'assets/kenney_car_kit/delivery.obj',
    'assets/kenney_car_kit/firetruck.obj',
    'assets/kenney_car_kit/garbage-truck.obj',
    'assets/kenney_car_kit/hatchback-sports.obj',
    'assets/kenney_car_kit/kart-oobi.obj',
    'assets/kenney_car_kit/kart-oodi.obj',
    'assets/kenney_car_kit/kart-ooli.obj',
    'assets/kenney_car_kit/kart-oopi.obj',
    'assets/kenney_car_kit/kart-oozi.obj',
    'assets/kenney_car_kit/police.obj',
    'assets/kenney_car_kit/race-future.obj',
    'assets/kenney_car_kit/race.obj',
    'assets/kenney_car_kit/sedan-sports.obj',
    'assets/kenney_car_kit/sedan.obj',
    'assets/kenney_car_kit/suv-luxury.obj',
    'assets/kenney_car_kit/suv.obj',
    'assets/kenney_car_kit/taxi.obj',
    'assets/kenney_car_kit/tractor-police.obj',
    'assets/kenney_car_kit/tractor-shovel.obj',
    'assets/kenney_car_kit/tractor.obj',
    'assets/kenney_car_kit/truck-flat.obj',
    'assets/kenney_car_kit/truck.obj',
    'assets/kenney_car_kit/van.obj',
];

const _kenneyNormalizeMatrix = new Float32Array(16);

function normalizeKenneyCarMesh(mesh) {
    // Kenney OBJ cars are authored as X=right, Y=up, Z=length. The demo car
    // model matrix expects local X=right, Y=forward, Z=up, centered like a
    // unit cube. Normalize bounds here so existing physics / headlights /
    // terrain alignment keep working for every downloaded car variant.
    let minX = Infinity, minY = Infinity, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    for (let i = 0; i < mesh.positions.length; i += 3) {
        const x = mesh.positions[i];
        const y = mesh.positions[i + 1];
        const z = mesh.positions[i + 2];
        minX = Math.min(minX, x); maxX = Math.max(maxX, x);
        minY = Math.min(minY, y); maxY = Math.max(maxY, y);
        minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z);
    }
    const cx = (minX + maxX) * 0.5;
    const cz = (minZ + maxZ) * 0.5;
    const sx = Math.max(1e-5, maxX - minX);
    const sy = Math.max(1e-5, maxY - minY);
    const sz = Math.max(1e-5, maxZ - minZ);

    const positions = new Float32Array(mesh.positions.length);
    const normals = new Float32Array(mesh.normals.length);

    // Batch-transform positions from Kenney OBJ space (X=right, Y=up,
    // Z=length) to demo car local space (X=right, Y=forward, Z=up).
    _kenneyNormalizeMatrix[0] = 1 / sx; _kenneyNormalizeMatrix[1] = 0; _kenneyNormalizeMatrix[2] = 0; _kenneyNormalizeMatrix[3] = 0;
    _kenneyNormalizeMatrix[4] = 0; _kenneyNormalizeMatrix[5] = 0; _kenneyNormalizeMatrix[6] = 1 / sy; _kenneyNormalizeMatrix[7] = 0;
    _kenneyNormalizeMatrix[8] = 0; _kenneyNormalizeMatrix[9] = 1 / sz; _kenneyNormalizeMatrix[10] = 0; _kenneyNormalizeMatrix[11] = 0;
    _kenneyNormalizeMatrix[12] = -cx / sx;
    _kenneyNormalizeMatrix[13] = -cz / sz;
    _kenneyNormalizeMatrix[14] = -minY / sy - 0.5;
    _kenneyNormalizeMatrix[15] = 1;
    sys.math.vec3TransformMat4Many(positions, mesh.positions, _kenneyNormalizeMatrix, mesh.positions.length / 3);

    for (let i = 0; i < mesh.normals.length; i += 3) {
        normals[i] = mesh.normals[i] / sx;
        normals[i + 1] = mesh.normals[i + 2] / sz;
        normals[i + 2] = mesh.normals[i + 1] / sy;
    }
    sys.math.vec3NormalizeMany(normals, normals, mesh.normals.length / 3);

    // Swapping Kenney's Y/Z axes changes handedness. Reverse each triangle so
    // back-face culling still treats the visible car shell as front-facing.
    const indices = new mesh.indices.constructor(mesh.indices.length);
    for (let i = 0; i < mesh.indices.length; i += 3) {
        indices[i] = mesh.indices[i];
        indices[i + 1] = mesh.indices[i + 2];
        indices[i + 2] = mesh.indices[i + 1];
    }
    return { positions, normals, uvs: mesh.uvs, indices };
}

function loadCarVariant(path) {
    try {
        const mesh = normalizeKenneyCarMesh(Mesh.fromOBJ(sys.assets.readText(path)));
        return { path, gpu: Mesh.upload(mesh) };
    } catch (e) {
        sys.log('Failed to load car mesh ' + path + ': ' + e);
        return null;
    }
}

// Keep all downloaded Kenney cars available for random selection, but upload
// only variants actually used by live cars. Uploading every OBJ at startup
// creates ~4 GL buffers per model, which competes with terrain chunks and can
// hit the runtime buffer cap after all 25 vehicle meshes are staged.
const carVariantCache = new Map();
let fallbackCarVariant = null;

function getFallbackCarVariant() {
    if (!fallbackCarVariant) {
        fallbackCarVariant = { path: 'fallback cube', gpu: Mesh.upload(Mesh.cube(1.0)) };
    }
    return fallbackCarVariant;
}

function getCarVariant(path) {
    let variant = carVariantCache.get(path);
    if (variant) return variant;
    variant = loadCarVariant(path);
    if (!variant) return null;
    carVariantCache.set(path, variant);
    return variant;
}

function randomCarVariant() {
    for (let tries = 0; tries < KENNEY_CAR_ASSETS.length; tries++) {
        const path = KENNEY_CAR_ASSETS[Math.floor(Math.random() * KENNEY_CAR_ASSETS.length)];
        const variant = getCarVariant(path);
        if (variant) return variant;
    }
    return getFallbackCarVariant();
}
const carProgram = createShaderProgram('car.vert', 'car.frag');
const rallySphereMesh = Mesh.upload(Mesh.sphere(1.0, 28));

// ============ Dynamic line buffer for force arrows ============

const linesProgram = createShaderProgram('lines.vert', 'lines.frag');
const hudOverlayProgram = createShaderProgram('hud_overlay.vert', 'hud_overlay.frag');
const headlightProgram = createShaderProgram('headlight.vert', 'headlight.frag');
const MAX_LINE_VERTICES = 256;
const lineData = new Float32Array(MAX_LINE_VERTICES * 6); // pos(3) + color(3)
const lineVBO = sys.gl.createBuffer('vertex', lineData);
const lineLayout = sys.gl.createVertexLayout();
sys.gl.setAttribute(lineLayout, 0 /* a_position */, lineVBO, 3, 'float', false, 6 * 4, 0);
sys.gl.setAttribute(lineLayout, 3 /* a_color    */, lineVBO, 3, 'float', false, 6 * 4, 3 * 4);

const MAX_ROAD_VERTICES = 4096;
const roadData = new Float32Array(MAX_ROAD_VERTICES * 6); // pos(3) + color(3)
const roadVBO = sys.gl.createBuffer('vertex', roadData);
const roadLayout = sys.gl.createVertexLayout();
sys.gl.setAttribute(roadLayout, 0 /* a_position */, roadVBO, 3, 'float', false, 6 * 4, 0);
sys.gl.setAttribute(roadLayout, 3 /* a_color    */, roadVBO, 3, 'float', false, 6 * 4, 3 * 4);

const HEADLIGHT_SEGMENTS = MOBILE_PROFILE ? 4 : 12;
const HEADLIGHT_UPDATE_INTERVAL_MS = 1000 / 60;
const MAX_HEADLIGHT_VERTICES = 768;
const HEADLIGHT_FLOATS_PER_VERT = 7; // pos(3) + color(4)
const headlightData = new Float32Array(MAX_HEADLIGHT_VERTICES * HEADLIGHT_FLOATS_PER_VERT);
const headlightVBO = sys.gl.createBuffer('vertex', headlightData);
const headlightLayout = sys.gl.createVertexLayout();
sys.gl.setAttribute(headlightLayout, 0 /* a_position */, headlightVBO, 3, 'float', false, HEADLIGHT_FLOATS_PER_VERT * 4, 0);
sys.gl.setAttribute(headlightLayout, 3 /* a_color    */, headlightVBO, 4, 'float', false, HEADLIGHT_FLOATS_PER_VERT * 4, 3 * 4);

let headlightCursor = 0;
let headlightDrawCount = 0;
let headlightGeometryValid = false;
let nextHeadlightUpdateMs = 0;
function headlightReset() { headlightCursor = 0; }
function headlightVertex(x, y, z, r, g, b, a) {
    if (headlightCursor + HEADLIGHT_FLOATS_PER_VERT > headlightData.length) return;
    headlightData[headlightCursor++] = x;
    headlightData[headlightCursor++] = y;
    headlightData[headlightCursor++] = z;
    headlightData[headlightCursor++] = r;
    headlightData[headlightCursor++] = g;
    headlightData[headlightCursor++] = b;
    headlightData[headlightCursor++] = a;
}
function headlightTri(a, b, c, colorA, colorB, colorC) {
    headlightVertex(a.x, a.y, a.z, colorA[0], colorA[1], colorA[2], colorA[3]);
    headlightVertex(b.x, b.y, b.z, colorB[0], colorB[1], colorB[2], colorB[3]);
    headlightVertex(c.x, c.y, c.z, colorC[0], colorC[1], colorC[2], colorC[3]);
}

let lineCursor = 0; // float index into lineData
function lineReset() { lineCursor = 0; }
function lineSeg(x1, y1, z1, x2, y2, z2, r, g, b) {
    if (lineCursor + 12 > lineData.length) return;
    lineData[lineCursor++] = x1; lineData[lineCursor++] = y1; lineData[lineCursor++] = z1;
    lineData[lineCursor++] = r; lineData[lineCursor++] = g; lineData[lineCursor++] = b;
    lineData[lineCursor++] = x2; lineData[lineCursor++] = y2; lineData[lineCursor++] = z2;
    lineData[lineCursor++] = r; lineData[lineCursor++] = g; lineData[lineCursor++] = b;
}

function lineArrow(sx, sy, sz, dx, dy, dz, scale, r, g, b) {
    const ex = sx + dx * scale, ey = sy + dy * scale, ez = sz + dz * scale;
    lineSeg(sx, sy, sz, ex, ey, ez, r, g, b);

    // Arrowhead: a small cross at the tip.
    let perpX, perpY, perpZ;
    if (Math.abs(dz) < 0.9) {
        perpX = dy; perpY = -dx; perpZ = 0;
    } else {
        perpX = 0; perpY = dz; perpZ = -dy;
    }
    const pl = Math.sqrt(perpX * perpX + perpY * perpY + perpZ * perpZ) || 1;
    perpX /= pl; perpY /= pl; perpZ /= pl;
    const headBase = 0.7;
    const baseX = sx + dx * scale * headBase;
    const baseY = sy + dy * scale * headBase;
    const baseZ = sz + dz * scale * headBase;
    const head = scale * 0.15;
    lineSeg(ex, ey, ez, baseX + perpX * head, baseY + perpY * head, baseZ + perpZ * head, r, g, b);
    lineSeg(ex, ey, ez, baseX - perpX * head, baseY - perpY * head, baseZ - perpZ * head, r, g, b);
}

// ============ Procedural road marker ============
// The road is a deterministic infinite rally route. A low-frequency FBM field
// bends its center line. It is rendered as terrain-following line art only:
// no terrain deformation, so it remains cheap and works with infinite chunks.

const ROAD_DRAW_RANGE = MOBILE_PROFILE ? 42 : 130;
const ROAD_STEP = MOBILE_PROFILE ? 12.0 : 6.0;
const ROAD_DASH_LEN = 11.0;
const ROAD_GAP_LEN = 9.0;
const ROAD_FLOW_SPEED = 18.0;
const ROAD_HALF_WIDTH = 2.1;
const ROAD_LIFT = 0.46;
const ROAD_SHADOW_LIFT = 0.40;
const ROAD_Y_ORIGIN = roadYRaw(0);
let roadCursor = 0;

function positiveMod(a, b) {
    return ((a % b) + b) % b;
}

function roadYRaw(x) {
    return fbm(x * 0.0065, 41.7, 3, 0.56) * 42
        + fbm(x * 0.0170 + 113.0, -21.3, 2, 0.50) * 11
        + Math.sin(x * 0.021 + 1.9) * 7.5;
}

function roadCenter(x) {
    // Anchor the initial road near the player spawn, then let deterministic
    // noise create long, believable curves across the world.
    const y = roadYRaw(x) - ROAD_Y_ORIGIN;
    return { x, y };
}

function roadTangent(x) {
    const a = roadCenter(x - 2.0);
    const b = roadCenter(x + 2.0);
    const dx = b.x - a.x;
    const dy = b.y - a.y;
    const len = Math.hypot(dx, dy) || 1;
    return { x: dx / len, y: dy / len };
}

function roadPoint(x, lateral, lift) {
    const c = roadCenter(x);
    const t = roadTangent(x);
    const nx = -t.y;
    const ny = t.x;
    const px = c.x + nx * lateral;
    const py = c.y + ny * lateral;
    return { x: px, y: py, z: getTerrainHeight(px, py) + lift };
}

function roadPointFromCenter(c, t, lateral, lift) {
    const nx = -t.y;
    const ny = t.x;
    const px = c.x + nx * lateral;
    const py = c.y + ny * lateral;
    return { x: px, y: py, z: getTerrainHeight(px, py) + lift };
}

function roadVertex(x, y, z, r, g, b) {
    if (roadCursor + 6 > roadData.length) return false;
    roadData[roadCursor++] = x;
    roadData[roadCursor++] = y;
    roadData[roadCursor++] = z;
    roadData[roadCursor++] = r;
    roadData[roadCursor++] = g;
    roadData[roadCursor++] = b;
    return true;
}

function roadSeg(a, b, r, g, bl) {
    if (roadCursor + 12 > roadData.length) return;
    roadVertex(a.x, a.y, a.z, r, g, bl);
    roadVertex(b.x, b.y, b.z, r, g, bl);
}

function roadDashedSeg(aX, bX, lateral, lift, r, g, b) {
    const a = roadPoint(aX, lateral, lift);
    const z = roadPoint(bX, lateral, lift);
    roadSeg(a, z, r, g, b);
}

function buildRoadLines(timestamp) {
    roadCursor = 0;
    const start = playerCar.x - ROAD_DRAW_RANGE;
    const end = playerCar.x + ROAD_DRAW_RANGE;
    let prevX = start;
    let prev = roadCenter(prevX);
    const flowOffset = timestamp * 0.001 * ROAD_FLOW_SPEED;

    for (let x = start + ROAD_STEP; x <= end; x += ROAD_STEP) {
        const cur = roadCenter(x);
        const dx = cur.x - prev.x;
        const dy = cur.y - prev.y;
        const segLen = Math.hypot(dx, dy);
        const invLen = segLen > 1e-4 ? 1 / segLen : 1;
        const tangent = { x: dx * invLen, y: dy * invLen };
        // Keep the dash phase independent from the visible start point, then
        // animate it with its own speed so the marks flow like a river instead
        // of being locked to the camera/player window.
        const midX = (prevX + x) * 0.5;
        const dashPhase = positiveMod(midX - flowOffset, ROAD_DASH_LEN + ROAD_GAP_LEN);

        if (dashPhase < ROAD_DASH_LEN) {
            // Cheap defined manga ink: a dark under-stroke plus two close
            // highlights. It is clearer than a single line but still much
            // lighter than the original multi-stroke version.
            roadSeg(roadPointFromCenter(prev, tangent, 0.00, ROAD_SHADOW_LIFT),
                roadPointFromCenter(cur, tangent, 0.00, ROAD_SHADOW_LIFT),
                0.05, 0.045, 0.04);
            roadSeg(roadPointFromCenter(prev, tangent, -0.16, ROAD_LIFT),
                roadPointFromCenter(cur, tangent, -0.16, ROAD_LIFT),
                1.00, 0.90, 0.46);
            roadSeg(roadPointFromCenter(prev, tangent, 0.16, ROAD_LIFT + 0.02),
                roadPointFromCenter(cur, tangent, 0.16, ROAD_LIFT + 0.02),
                1.00, 0.98, 0.82);
        }

        // Sparse side hatches are also anchored to deterministic world cells.
        const hatchCell = Math.floor(x / 30.0);
        if (hatchCell !== Math.floor(prevX / 30.0)) {
            const hx = hatchCell * 30.0;
            const hc = roadCenter(hx);
            const side = hash2(hatchCell, 9301) < 0.5 ? -1 : 1;
            const a = roadPointFromCenter(hc, tangent, side * (ROAD_HALF_WIDTH + 0.3), ROAD_LIFT + 0.01);
            const n = { x: -tangent.y * side, y: tangent.x * side };
            const bx = a.x + n.x * 1.8 - tangent.x * 1.0;
            const by = a.y + n.y * 1.8 - tangent.y * 1.0;
            const bpt = { x: bx, y: by, z: getTerrainHeight(bx, by) + ROAD_LIFT + 0.01 };
            roadSeg(a, bpt, 0.08, 0.07, 0.055);
        }

        prevX = x;
        prev = cur;
    }
}

function drawRoad(timestamp) {
    buildRoadLines(timestamp);
    if (roadCursor <= 0) return;
    sys.gl.updateBuffer(roadVBO, roadData, 0);
    linesProgram.uniformMatrix4('u_mvp', viewProj);
    linesProgram.drawMesh(roadLayout, {
        mode: 'lines',
        first: 0,
        count: roadCursor / 6,
        depthTest: true,
        depthWrite: false,
        cull: 'none',
        blend: 'none',
    });
}

function headlightPoint(x, y, u, halfWidth, lift) {
    return { x, y, z: getTerrainHeight(x, y) + lift, u, v: halfWidth };
}

function isHeadlightVisible(sx, sy, lampZ, tx, ty, tz) {
    // Approximate a line-of-sight test from the raised lamp to the lit terrain
    // point. If a ridge crosses the ray, stop the beam so light does not appear
    // on the far side of terrain that should block it.
    const samples = 7;
    for (let i = 1; i < samples; i++) {
        const t = i / samples;
        const x = sx + (tx - sx) * t;
        const y = sy + (ty - sy) * t;
        const rayZ = lampZ + (tz - lampZ) * t;
        if (getTerrainHeight(x, y) > rayZ + 0.08) return false;
    }
    return true;
}

function addHeadlightCone(car, side, intensity) {
    const vehicleScale = physics.vehicleScale;
    const fwdX = Math.cos(car.heading);
    const fwdY = Math.sin(car.heading);
    const rightX = fwdY;
    const rightY = -fwdX;
    const startDist = CAR_LENGTH * vehicleScale * 0.58;
    const laneOffset = side * CAR_WIDTH * vehicleScale * 0.23;
    const beamLength = 18.0 * Math.sqrt(vehicleScale);
    const maxHalfWidth = 3.2 * Math.sqrt(vehicleScale);
    const sx = car.x + fwdX * startDist + rightX * laneOffset;
    const sy = car.y + fwdY * startDist + rightY * laneOffset;
    const lampZ = getTerrainHeight(sx, sy) + CAR_HEIGHT * vehicleScale * 0.42;
    const lift = Math.max(0.11, 0.035 * vehicleScale);

    let prevCenter = headlightPoint(sx, sy, 0.0, 0.0, lift);
    let prevLeft = prevCenter;
    let prevRight = prevCenter;
    for (let segment = 1; segment <= HEADLIGHT_SEGMENTS; segment++) {
        const t = segment / HEADLIGHT_SEGMENTS;
        const centerX = sx + fwdX * beamLength * t;
        const centerY = sy + fwdY * beamLength * t;

        const halfWidth = maxHalfWidth * Math.pow(t, 0.82);
        const center = headlightPoint(centerX, centerY, t, 0.0, lift);
        const left = headlightPoint(centerX - rightX * halfWidth,
            centerY - rightY * halfWidth,
            t, -halfWidth / maxHalfWidth, lift);
        const right = headlightPoint(centerX + rightX * halfWidth,
            centerY + rightY * halfWidth,
            t, halfWidth / maxHalfWidth, lift);
        if (!isHeadlightVisible(sx, sy, lampZ, center.x, center.y, center.z) ||
            !isHeadlightVisible(sx, sy, lampZ, left.x, left.y, left.z) ||
            !isHeadlightVisible(sx, sy, lampZ, right.x, right.y, right.z)) {
            break;
        }

        const a0 = (1.0 - prevCenter.u) * (1.0 - prevCenter.u) * 0.36 * intensity;
        const a1 = (1.0 - t) * (1.0 - t) * 0.22 * intensity;
        const c0 = [1.0, 0.86, 0.46, a0];
        const c1 = [1.0, 0.78, 0.30, a1];
        const edge0 = [1.0, 0.72, 0.24, 0.0];
        const edge1 = [1.0, 0.72, 0.24, 0.0];

        // Four narrow triangles per segment: a bright centerline fading to
        // transparent edges. This conforms to uneven terrain and still avoids
        // drawing light behind ridges because the loop stops on occlusion.
        headlightTri(prevCenter, prevLeft, left, c0, edge0, edge1);
        headlightTri(prevCenter, left, center, c0, edge1, c1);
        headlightTri(prevCenter, center, right, c0, c1, edge1);
        headlightTri(prevCenter, right, prevRight, c0, edge1, edge0);

        prevCenter = center;
        prevLeft = left;
        prevRight = right;
    }
}

function drawHeadlights(timestamp) {
    if (MOBILE_PROFILE) return;
    if (headlightNightFactor <= 0.02) {
        headlightDrawCount = 0;
        headlightGeometryValid = false;
        nextHeadlightUpdateMs = timestamp;
        return;
    }

    if (!headlightGeometryValid || timestamp >= nextHeadlightUpdateMs) {
        headlightReset();
        for (const car of cars) {
            addHeadlightCone(car, -1, headlightNightFactor);
            addHeadlightCone(car, +1, headlightNightFactor);
        }
        headlightDrawCount = headlightCursor / HEADLIGHT_FLOATS_PER_VERT;
        if (headlightDrawCount > 0) {
            sys.gl.updateBuffer(headlightVBO, headlightData, 0);
        }
        headlightGeometryValid = true;
        if (nextHeadlightUpdateMs === 0) nextHeadlightUpdateMs = timestamp;
        do {
            nextHeadlightUpdateMs += HEADLIGHT_UPDATE_INTERVAL_MS;
        } while (nextHeadlightUpdateMs <= timestamp);
    }

    if (headlightDrawCount <= 0) return;
    headlightProgram.uniformMatrix4('u_mvp', viewProj);
    headlightProgram.drawMesh(headlightLayout, {
        mode: 'triangles',
        first: 0,
        count: headlightDrawCount,
        depthTest: true,
        depthWrite: false,
        cull: 'none',
        blend: 'alpha',
    });
}

// ============ Shadow disk (terrain-hugging dynamic mesh) ============
// A small radial triangle mesh around the car footprint. Every radial ring
// samples the terrain height, not just the rim, so the projected shadow follows
// curved/noisy terrain instead of cutting through it and failing the depth test.

const SHADOW_SEGMENTS = MOBILE_PROFILE ? 8 : 24;
const SHADOW_RINGS = MOBILE_PROFILE ? 1 : 4;
const SHADOW_VERTICES_PER_CAR = 1 + SHADOW_SEGMENTS * SHADOW_RINGS;
const SHADOW_INDICES_PER_CAR = SHADOW_SEGMENTS * (3 + (SHADOW_RINGS - 1) * 6);
const SHADOW_FLOATS_PER_VERT = 5;                // pos(3) + uv(2)
const MAX_SHADOW_CARS = MOBILE_PROFILE ? 1 : 4;

function createShadowGPU() {
    // One dynamic mesh contains all car shadows for the frame, so Android pays
    // for one buffer upload and one pass instead of one per vehicle. Each ring
    // point is shared by adjacent triangles, avoiding duplicate terrain samples
    // while producing exactly the same interpolated geometry as the old stream.
    const data = new Float32Array(SHADOW_VERTICES_PER_CAR * MAX_SHADOW_CARS * SHADOW_FLOATS_PER_VERT);
    const indices = new Uint16Array(SHADOW_INDICES_PER_CAR * MAX_SHADOW_CARS);
    let ii = 0;
    for (let car = 0; car < MAX_SHADOW_CARS; car++) {
        const base = car * SHADOW_VERTICES_PER_CAR;
        for (let seg = 0; seg < SHADOW_SEGMENTS; seg++) {
            const next = (seg + 1) % SHADOW_SEGMENTS;
            for (let ring = 0; ring < SHADOW_RINGS; ring++) {
                const outerA = base + 1 + ring * SHADOW_SEGMENTS + seg;
                const outerB = base + 1 + ring * SHADOW_SEGMENTS + next;
                if (ring === 0) {
                    indices[ii++] = base;
                    indices[ii++] = outerA;
                    indices[ii++] = outerB;
                } else {
                    const innerA = base + 1 + (ring - 1) * SHADOW_SEGMENTS + seg;
                    const innerB = base + 1 + (ring - 1) * SHADOW_SEGMENTS + next;
                    indices[ii++] = innerA;
                    indices[ii++] = outerA;
                    indices[ii++] = outerB;
                    indices[ii++] = innerA;
                    indices[ii++] = outerB;
                    indices[ii++] = innerB;
                }
            }
        }
    }
    const vbo = sys.gl.createBuffer('vertex', data);
    const ibo = sys.gl.createBuffer('index', indices);
    const layout = sys.gl.createVertexLayout();
    sys.gl.setAttribute(layout, 0 /* a_position */, vbo, 3, 'float', false, SHADOW_FLOATS_PER_VERT * 4, 0);
    sys.gl.setAttribute(layout, 1 /* a_uv       */, vbo, 2, 'float', false, SHADOW_FLOATS_PER_VERT * 4, 3 * 4);
    sys.gl.setIndexBuffer(layout, ibo, 'u16');
    const program = createShaderProgram('shadow.vert', 'shadow.frag');
    return { data, vbo, ibo, layout, program };
}
const shadowGPU = createShadowGPU();

// Pre-compute ring direction unit vectors.
const shadowRing = [];
for (let i = 0; i < SHADOW_SEGMENTS; i++) {
    const a = (i / SHADOW_SEGMENTS) * Math.PI * 2;
    shadowRing.push({ c: Math.cos(a), s: Math.sin(a) });
}

const radarPlayerPath = sys.path.create();

const SHADOW_LIFT = 0.10; // vertical offset to avoid z-fighting on rough terrain

function shadowVertex(out, i, x, y, u, v, lift) {
    out[i++] = x;
    out[i++] = y;
    out[i++] = getTerrainHeight(x, y) + lift;
    out[i++] = u;
    out[i++] = v;
    return i;
}

function buildShadowMeshAt(out, offset, cx, cy, radius) {
    let i = offset;
    const lift = Math.max(SHADOW_LIFT, radius * 0.055);
    i = shadowVertex(out, i, cx, cy, 0, 0, lift);
    for (let ring = 1; ring <= SHADOW_RINGS; ring++) {
        const radial = ring / SHADOW_RINGS;
        for (let seg = 0; seg < SHADOW_SEGMENTS; seg++) {
            const direction = shadowRing[seg];
            const x = cx + direction.c * radius * radial;
            const y = cy + direction.s * radius * radial;
            i = shadowVertex(out, i, x, y,
                direction.c * radial,
                direction.s * radial,
                lift);
        }
    }
    return i;
}

// ============ Car State ============

function makeTerrainCar(opts) {
    return makeCar({ mesh: randomCarVariant(), ...opts });
}

const cars = [
    makeTerrainCar({ x: 0, y: 0, color: new Float32Array([0.85, 0.20, 0.20]) }),
];
if (!MOBILE_PROFILE) {
    cars.push(
        makeTerrainCar({
            x: 6, y: 4, heading: Math.PI, isAI: true,
            color: new Float32Array([0.20, 0.45, 0.90])
        }),
        makeTerrainCar({
            x: -10, y: 7, heading: Math.PI * 0.35, isAI: true,
            color: new Float32Array([0.95, 0.72, 0.18])
        }),
        makeTerrainCar({
            x: 12, y: -9, heading: -Math.PI * 0.65, isAI: true,
            color: new Float32Array([0.24, 0.78, 0.38])
        }),
    );
}
const playerCar = cars[0];
for (let i = 1; i < cars.length; i++) pickAIControls(cars[i]);

// ============ Rally target ============
// A moving rally coordinate. It follows the generated road centerline at a
// steady speed. It is rendered as a metallic sphere and shown on radar only
// when enabled from the UI.

const TARGET_MIN_DIST = 34;
const TARGET_MAX_DIST = 78;
const TARGET_EVADE_DIST = 22;
const TARGET_BASE_SPEED = 42.0;
const TARGET_EVADE_SPEED = 62.0;
const TARGET_SPHERE_RADIUS = 3.2;
const TARGET_WANDER_INTERVAL_MS = 4200;
let nextTargetWanderMs = 0;
const rallyTarget = { x: 36, y: 18, z: 0, vx: 0, vy: 0, angle: 0, turnSign: 1, roadX: 36, roadSpeed: 0 };

function wrapAngle(a) {
    while (a > Math.PI) a -= Math.PI * 2;
    while (a < -Math.PI) a += Math.PI * 2;
    return a;
}

function chooseRallyTarget() {
    const sign = Math.random() < 0.5 ? -1 : 1;
    const dist = TARGET_MIN_DIST + Math.random() * (TARGET_MAX_DIST - TARGET_MIN_DIST);
    rallyTarget.roadX = playerCar.x + sign * dist;
    const p = roadCenter(rallyTarget.roadX);
    rallyTarget.x = p.x;
    rallyTarget.y = p.y;
    rallyTarget.z = getTerrainHeight(p.x, p.y);
    rallyTarget.vx = 0;
    rallyTarget.vy = 0;
    rallyTarget.roadSpeed = 0;
    rallyTarget.angle = 0;
    rallyTarget.turnSign = sign;
}

function updateRallyTarget(timestamp, dt) {
    if (nextTargetWanderMs === 0) {
        chooseRallyTarget();
        nextTargetWanderMs = 1;
    }

    const targetRoadSpeed = rallyTarget.turnSign * TARGET_BASE_SPEED;
    const follow = 1 - Math.exp(-1.8 * dt);
    rallyTarget.roadSpeed += (targetRoadSpeed - rallyTarget.roadSpeed) * follow;

    const oldX = rallyTarget.x;
    const oldY = rallyTarget.y;
    rallyTarget.roadX += rallyTarget.roadSpeed * dt;
    const p = roadCenter(rallyTarget.roadX);
    rallyTarget.x = p.x;
    rallyTarget.y = p.y;
    rallyTarget.z = getTerrainHeight(rallyTarget.x, rallyTarget.y);
    const invDt = dt > 1e-4 ? 1 / dt : 0;
    rallyTarget.vx = (rallyTarget.x - oldX) * invDt;
    rallyTarget.vy = (rallyTarget.y - oldY) * invDt;
}

function gatherRallyAIControls(car, dt) {
    const wander = gatherAIControls(car, dt);
    const leadTime = 0.95;
    const dx = rallyTarget.x + rallyTarget.vx * leadTime - car.x;
    const dy = rallyTarget.y + rallyTarget.vy * leadTime - car.y;
    const dist = Math.hypot(dx, dy);
    const desired = Math.atan2(dy, dx);
    const err = wrapAngle(desired - car.heading);
    const steerToTarget = -Math.max(-1, Math.min(1, err * 1.35));
    const alignment = Math.max(0.15, 1.0 - Math.min(1.0, Math.abs(err) / Math.PI));
    return {
        throttle: Math.max(-0.25, Math.min(1, (0.62 + 0.38 * alignment) + wander.throttle * 0.10)),
        steer: Math.max(-1, Math.min(1, steerToTarget + wander.steer * 0.28)),
    };
}

const terrainPhysics = {
    getHeight: getTerrainHeight,
    getNormal: getTerrainNormal,
};

let selectedParamIndex = 0;
const paramButtons = []; // populated lazily in drawPhysicsPanel
let lastMouseDown = false;

// ============ Camera State ============

let cameraRotation = Math.PI / 4;
let followCameraRotation = cameraRotation;
let followCameraOffset = Math.PI;
let cameraAngle = Math.PI / 6;
let cameraDistance = MOBILE_PROFILE ? 22 : 25;

function angleDelta(target, current) {
    let d = (target - current) % (Math.PI * 2);
    if (d > Math.PI) d -= Math.PI * 2;
    if (d < -Math.PI) d += Math.PI * 2;
    return d;
}

function getCameraPosition(targetX, targetY, targetZ, out) {
    const cosRot = Math.cos(cameraRotation);
    const sinRot = Math.sin(cameraRotation);
    const cosAng = Math.cos(cameraAngle);
    const sinAng = Math.sin(cameraAngle);
    out[0] = targetX + cameraDistance * cosAng * cosRot;
    out[1] = targetY + cameraDistance * cosAng * sinRot;
    out[2] = targetZ + cameraDistance * sinAng;
}

// ============ Touch Input Processing ============

function updateTouchInput(input) {
    const mx = input.mouse.x;
    const my = input.mouse.y;
    const touching = input.mouse.left;
    const justPressed = touching && !lastMouseDown;
    activeButtons.clear();

    // View toggle button (rising-edge).
    if (justPressed
        && mx >= VIEW_BTN_X - VIEW_BTN_W / 2 && mx <= VIEW_BTN_X + VIEW_BTN_W / 2
        && my >= VIEW_BTN_Y - VIEW_BTN_H / 2 && my <= VIEW_BTN_Y + VIEW_BTN_H / 2) {
        cycleViewMode();
        lastMouseDown = touching;
        return;
    }

    // Rally target visibility toggle button (rising-edge). The target still
    // drives AI behavior, but the sphere/radar marker are hidden by default.
    if (justPressed
        && mx >= TARGET_BTN_X - TARGET_BTN_W / 2 && mx <= TARGET_BTN_X + TARGET_BTN_W / 2
        && my >= TARGET_BTN_Y - TARGET_BTN_H / 2 && my <= TARGET_BTN_Y + TARGET_BTN_H / 2) {
        showRallyTarget = !showRallyTarget;
        lastMouseDown = touching;
        return;
    }

    // Param panel hit-test (rising-edge clicks). Use buttons that were laid
    // out on the previous frame.
    if (justPressed) {
        for (const btn of paramButtons) {
            if (mx >= btn.x && mx <= btn.x + btn.w && my >= btn.y && my <= btn.y + btn.h) {
                if (btn.kind === 'select') {
                    selectedParamIndex = btn.index;
                } else if (btn.kind === 'inc') {
                    selectedParamIndex = btn.index;
                    adjustParam(btn.index, +physicsParams[btn.index].step);
                } else if (btn.kind === 'dec') {
                    selectedParamIndex = btn.index;
                    adjustParam(btn.index, -physicsParams[btn.index].step);
                } else if (btn.kind === 'reset') {
                    resetPhysics();
                }
                lastMouseDown = touching;
                return; // panel click consumed; no joystick / camera buttons
            }
        }
    }

    // Param panel covers the top-right; ignore that vertical band for camera buttons.
    const inParamPanel = mx > screenWidth - PARAM_PANEL_WIDTH - 10
        && my < PARAM_PANEL_HEIGHT + 10;
    const inTargetButton = mx >= TARGET_BTN_X - TARGET_BTN_W / 2 && mx <= TARGET_BTN_X + TARGET_BTN_W / 2
        && my >= TARGET_BTN_Y - TARGET_BTN_H / 2 && my <= TARGET_BTN_Y + TARGET_BTN_H / 2;

    if (touching && !inParamPanel && !inTargetButton) {
        if (mx < screenWidth / 2) {
            touchJoystickActive = true;
            const offsetX = (mx - JOYSTICK_X) / JOYSTICK_RADIUS;
            const offsetY = (my - JOYSTICK_Y) / JOYSTICK_RADIUS;
            const magnitude = Math.sqrt(offsetX * offsetX + offsetY * offsetY);
            if (magnitude > JOYSTICK_DEAD_ZONE) {
                const clampedMag = Math.min(magnitude, 1.0);
                touchJoystickX = (offsetX / magnitude) * clampedMag;
                touchJoystickY = (offsetY / magnitude) * clampedMag;
            } else {
                touchJoystickX = 0;
                touchJoystickY = 0;
            }
        } else {
            touchJoystickActive = false;
            touchJoystickX = 0;
            touchJoystickY = 0;
            for (const btn of cameraButtons) {
                const dist = Math.sqrt((mx - btn.x) ** 2 + (my - btn.y) ** 2);
                if (dist < BUTTON_SIZE / 2 + 10) activeButtons.add(btn.action);
            }
        }
    } else {
        touchJoystickActive = false;
        touchJoystickX = 0;
        touchJoystickY = 0;
    }

    lastMouseDown = touching;
}

// ============ Physics-parameter keyboard shortcuts ============

// SDL scancodes: 1..8 = 30..37, 0 = 39, [ = 47, ] = 48, - = 45, = = 46,
// Backspace = 42 (used for reset)
const DIGIT_SCANCODES = [30, 31, 32, 33, 34, 35, 36, 37];

function updateParamKeyboard(dt) {
    // Switch selected parameter on digit press.
    for (let i = 0; i < physicsParams.length && i < DIGIT_SCANCODES.length; i++) {
        if (sys.input.isKeyPressed(DIGIT_SCANCODES[i])) {
            selectedParamIndex = i;
        }
    }
    // Reset all parameters with Backspace.
    if (sys.input.isKeyPressed(42)) resetPhysics();

    // Cycle camera view with V (SDL scancode 25).
    if (sys.input.isKeyPressed(25)) cycleViewMode();

    // Continuous adjust while holding [ / - or ] / =.
    // Rate: ~4 steps per second (so it feels snappy without being twitchy).
    const def = physicsParams[selectedParamIndex];
    if (!def) return;
    const dec = sys.input.isKeyDown(47) || sys.input.isKeyDown(45);
    const inc = sys.input.isKeyDown(48) || sys.input.isKeyDown(46);
    if (dec || inc) {
        const dir = (inc ? 1 : 0) - (dec ? 1 : 0);
        adjustParam(selectedParamIndex, dir * def.step * 4 * dt);
    }
}

// ============ Physics ============

function gatherPlayerControls() {
    let throttle = 0;
    let steer = 0;
    if (sys.input.isKeyDown(26) || sys.input.isKeyDown(82)) throttle += 1; // W / Up
    if (sys.input.isKeyDown(22) || sys.input.isKeyDown(81)) throttle -= 1; // S / Down
    if (sys.input.isKeyDown(4) || sys.input.isKeyDown(80)) steer -= 1; // A / Left
    if (sys.input.isKeyDown(7) || sys.input.isKeyDown(79)) steer += 1; // D / Right
    if (activeButtons.has('throttleFwd')) throttle += 1;
    if (activeButtons.has('throttleRev')) throttle -= 1;
    if (activeButtons.has('steerLeft')) steer -= 1;
    if (activeButtons.has('steerRight')) steer += 1;
    if (touchJoystickActive) {
        throttle += -touchJoystickY;
        steer += touchJoystickX;
    }
    return {
        throttle: Math.max(-1, Math.min(1, throttle)),
        steer: Math.max(-1, Math.min(1, steer)),
    };
}

// ============ Camera Controls ============

function updateCamera(dt) {
    const rotSpeed = 1.5 * dt;
    const angleSpeed = 0.8 * dt;
    const distSpeed = 10 * dt;
    const rotInput = (sys.input.isKeyDown(8) || activeButtons.has('rotRight') ? 1 : 0)
        - (sys.input.isKeyDown(20) || activeButtons.has('rotLeft') ? 1 : 0);
    if (viewMode === VIEW_FOLLOW) {
        // Same orbit camera as the initial view, but its azimuth eases toward
        // a point behind the car. Manual rotate adjusts the follow offset.
        followCameraOffset += rotInput * rotSpeed;
        const desired = playerCar.heading + followCameraOffset;
        followCameraRotation += angleDelta(desired, followCameraRotation) * (1 - Math.exp(-2.4 * dt));
    } else if (rotInput !== 0) {
        cameraRotation += rotInput * rotSpeed;
        followCameraRotation = cameraRotation;
    }
    if (sys.input.isKeyDown(21)) cameraAngle = Math.min(Math.PI / 2 - 0.1, cameraAngle + angleSpeed);
    if (sys.input.isKeyDown(9)) cameraAngle = Math.max(0.1, cameraAngle - angleSpeed);
    if (sys.input.isKeyDown(23)) cameraDistance = Math.max(8, cameraDistance - distSpeed);
    if (sys.input.isKeyDown(10)) cameraDistance = Math.min(MOBILE_PROFILE ? 34 : 50, cameraDistance + distSpeed);
    if (activeButtons.has('angleUp')) cameraAngle = Math.min(Math.PI / 2 - 0.1, cameraAngle + angleSpeed);
    if (activeButtons.has('angleDown')) cameraAngle = Math.max(0.1, cameraAngle - angleSpeed);
    if (activeButtons.has('zoomIn')) cameraDistance = Math.max(8, cameraDistance - distSpeed);
    if (activeButtons.has('zoomOut')) cameraDistance = Math.min(MOBILE_PROFILE ? 34 : 50, cameraDistance + distSpeed);
}

// ============ GL state (matrices, reusable buffers) ============

const proj = new Float32Array(16);
const view = new Float32Array(16);
const viewProj = new Float32Array(16);
const terrainModel = new Float32Array(16);
const terrainMVP = new Float32Array(16);
const palmModel = new Float32Array(16);
const palmMVP = new Float32Array(16);
const carModel = new Float32Array(16);
const carMVP = new Float32Array(16);
const rallyModel = new Float32Array(16);
const rallyMVP = new Float32Array(16);
const eye = new Float32Array(3);
const target = new Float32Array(3);
const upZ = new Float32Array([0, 0, 1]);

const SKY_COLOR_RGB = new Float32Array([0xe8 / 255, 0xcf / 255, 0x9a / 255]);
const FOG_COLOR_RGB = new Float32Array([0xe8 / 255, 0xcf / 255, 0x9a / 255]);
const LIGHT_DIR = new Float32Array([0.55, 0.35, 0.76]);
const LIGHT_COLOR = new Float32Array([1.0, 0.88, 0.68]);
const DAY_SKY_RGB = [0xe8 / 255, 0xcf / 255, 0x9a / 255];
const NIGHT_SKY_RGB = [0x12 / 255, 0x18 / 255, 0x2f / 255];
const SUNSET_SKY_RGB = [0xf0 / 255, 0x84 / 255, 0x46 / 255];
const DAY_FOG_RGB = [0xe8 / 255, 0xcf / 255, 0x9a / 255];
const NIGHT_FOG_RGB = [0x1b / 255, 0x21 / 255, 0x38 / 255];
const SUNSET_FOG_RGB = [0xe0 / 255, 0x82 / 255, 0x52 / 255];
const DAY_LIGHT_RGB = [1.0, 0.86, 0.62];
const NIGHT_LIGHT_RGB = [0.28, 0.36, 0.68];
const RALLY_SPHERE_RGB = new Float32Array([0.82, 0.86, 0.90]);
const DAY_NIGHT_PERIOD_MS = 90000;
const FOG_START = MOBILE_PROFILE ? 30.0 : 38.0;
const FOG_END = MOBILE_PROFILE ? 72.0 : 105.0;
let skyColorARGB = 0xffe8cf9a;
let ambientLight = 0.42;
let headlightNightFactor = 0.0;

const RADAR_X = 92;
const RADAR_Y = 220;
const RADAR_RADIUS = 58;
const RADAR_RANGE = 85; // world units shown from center to edge

let currentFPS = 0;
let smoothedFPS = 0;

function smoothstep(edge0, edge1, x) {
    const t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}

function mixNumber(a, b, t) { return a + (b - a) * t; }

function mixColor(out, a, b, t) {
    out[0] = mixNumber(a[0], b[0], t);
    out[1] = mixNumber(a[1], b[1], t);
    out[2] = mixNumber(a[2], b[2], t);
}

function addColor(out, color, amount) {
    out[0] = Math.min(1, out[0] + color[0] * amount);
    out[1] = Math.min(1, out[1] + color[1] * amount);
    out[2] = Math.min(1, out[2] + color[2] * amount);
}

function rgbToArgb(rgb) {
    const r = Math.max(0, Math.min(255, Math.round(rgb[0] * 255)));
    const g = Math.max(0, Math.min(255, Math.round(rgb[1] * 255)));
    const b = Math.max(0, Math.min(255, Math.round(rgb[2] * 255)));
    return (0xff000000 | (r << 16) | (g << 8) | b) >>> 0;
}

function updateDayNightLighting(timestamp) {
    // A slow continuous cycle: noon -> sunset -> moonlit night -> sunrise.
    const phase = (timestamp / DAY_NIGHT_PERIOD_MS) * Math.PI * 2 + Math.PI * 0.30;
    const sunHeight = Math.sin(phase);
    const daylight = smoothstep(-0.18, 0.42, sunHeight);
    const twilight = (1.0 - Math.abs(daylight * 2.0 - 1.0)) * smoothstep(-0.35, 0.25, sunHeight);
    headlightNightFactor = 1.0 - smoothstep(0.16, 0.46, daylight);

    ambientLight = mixNumber(0.10, 0.43, daylight) + twilight * 0.08;
    mixColor(SKY_COLOR_RGB, NIGHT_SKY_RGB, DAY_SKY_RGB, daylight);
    addColor(SKY_COLOR_RGB, SUNSET_SKY_RGB, twilight * 0.28);
    mixColor(FOG_COLOR_RGB, NIGHT_FOG_RGB, DAY_FOG_RGB, daylight);
    addColor(FOG_COLOR_RGB, SUNSET_FOG_RGB, twilight * 0.22);
    mixColor(LIGHT_COLOR, NIGHT_LIGHT_RGB, DAY_LIGHT_RGB, daylight);

    const x = Math.cos(phase) * 0.62;
    const y = 0.36;
    const z = Math.max(0.10, Math.abs(sunHeight));
    const len = Math.sqrt(x * x + y * y + z * z) || 1;
    LIGHT_DIR[0] = x / len;
    LIGHT_DIR[1] = y / len;
    LIGHT_DIR[2] = z / len;
    skyColorARGB = rgbToArgb(SKY_COLOR_RGB);
}

// Build a 4x4 column-major model matrix for the car so its local axes
// (X=right, Y=forward, Z=up) align with the terrain frame.
const carAxesScratch = {
    right: { x: 1, y: 0, z: 0 },
    forward: { x: 0, y: 1, z: 0 },
    up: { x: 0, y: 0, z: 1 },
};

function getCarVisualAxes(car) {
    const heading = car.heading;
    let fwdX = Math.cos(heading), fwdY = Math.sin(heading), fwdZ = 0;

    if (car.grounded) {
        const normal = getTerrainNormal(car.x, car.y);
        const dotFN = fwdX * normal.x + fwdY * normal.y + fwdZ * normal.z;
        fwdX -= dotFN * normal.x;
        fwdY -= dotFN * normal.y;
        fwdZ -= dotFN * normal.z;
        const fl = Math.sqrt(fwdX * fwdX + fwdY * fwdY + fwdZ * fwdZ) || 1;
        fwdX /= fl; fwdY /= fl; fwdZ /= fl;

        let rightX = fwdY * normal.z - fwdZ * normal.y;
        let rightY = fwdZ * normal.x - fwdX * normal.z;
        let rightZ = fwdX * normal.y - fwdY * normal.x;
        const rl = Math.sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ) || 1;
        rightX /= rl; rightY /= rl; rightZ /= rl;

        carAxesScratch.right.x = rightX; carAxesScratch.right.y = rightY; carAxesScratch.right.z = rightZ;
        carAxesScratch.forward.x = fwdX; carAxesScratch.forward.y = fwdY; carAxesScratch.forward.z = fwdZ;
        carAxesScratch.up.x = normal.x; carAxesScratch.up.y = normal.y; carAxesScratch.up.z = normal.z;
        return carAxesScratch;
    }

    // Airborne attitude: start from yaw, then apply inertial pitch around the
    // right axis and roll around the pitched forward axis. This preserves the
    // takeoff attitude without sampling terrain below the car.
    const rightX0 = fwdY, rightY0 = -fwdX, rightZ0 = 0;
    const cp = Math.cos(car.pitch), sp = Math.sin(car.pitch);
    const fwdPX = fwdX * cp;
    const fwdPY = fwdY * cp;
    const fwdPZ = sp;
    const upPX = -fwdX * sp;
    const upPY = -fwdY * sp;
    const upPZ = cp;
    const cr = Math.cos(car.roll), sr = Math.sin(car.roll);

    carAxesScratch.right.x = rightX0 * cr - upPX * sr;
    carAxesScratch.right.y = rightY0 * cr - upPY * sr;
    carAxesScratch.right.z = rightZ0 * cr - upPZ * sr;
    carAxesScratch.forward.x = fwdPX;
    carAxesScratch.forward.y = fwdPY;
    carAxesScratch.forward.z = fwdPZ;
    carAxesScratch.up.x = rightX0 * sr + upPX * cr;
    carAxesScratch.up.y = rightY0 * sr + upPY * cr;
    carAxesScratch.up.z = rightZ0 * sr + upPZ * cr;
    return carAxesScratch;
}

function buildCarModel(out, car) {
    const vehicleScale = physics.vehicleScale;
    const axes = getCarVisualAxes(car);
    const right = axes.right, forward = axes.forward, up = axes.up;
    const cx = car.x, cy = car.y, cz = car.z;

    // Column 0 = right * width
    out[0] = right.x * CAR_WIDTH * vehicleScale;
    out[1] = right.y * CAR_WIDTH * vehicleScale;
    out[2] = right.z * CAR_WIDTH * vehicleScale;
    out[3] = 0;
    // Column 1 = forward * length
    out[4] = forward.x * CAR_LENGTH * vehicleScale;
    out[5] = forward.y * CAR_LENGTH * vehicleScale;
    out[6] = forward.z * CAR_LENGTH * vehicleScale;
    out[7] = 0;
    // Column 2 = up * height
    out[8] = up.x * CAR_HEIGHT * vehicleScale;
    out[9] = up.y * CAR_HEIGHT * vehicleScale;
    out[10] = up.z * CAR_HEIGHT * vehicleScale;
    out[11] = 0;
    // Column 3 = translation (above contact/body center by half height along up)
    out[12] = cx + up.x * CAR_HEIGHT * vehicleScale * 0.5;
    out[13] = cy + up.y * CAR_HEIGHT * vehicleScale * 0.5;
    out[14] = cz + up.z * CAR_HEIGHT * vehicleScale * 0.5;
    out[15] = 1;
}

function buildTerrainObjectModel(out, cx, cy, cz, heading, scale) {
    buildTerrainObjectModelAt(out, 0, cx, cy, cz, heading, scale);
}

function buildTerrainObjectModelAt(out, offset, cx, cy, cz, heading, scale) {
    const normal = getTerrainNormal(cx, cy);
    let fwdX = Math.cos(heading), fwdY = Math.sin(heading), fwdZ = 0;
    const dotFN = fwdX * normal.x + fwdY * normal.y + fwdZ * normal.z;
    fwdX -= dotFN * normal.x;
    fwdY -= dotFN * normal.y;
    fwdZ -= dotFN * normal.z;
    const fl = Math.sqrt(fwdX * fwdX + fwdY * fwdY + fwdZ * fwdZ) || 1;
    fwdX /= fl; fwdY /= fl; fwdZ /= fl;

    let rightX = fwdY * normal.z - fwdZ * normal.y;
    let rightY = fwdZ * normal.x - fwdX * normal.z;
    let rightZ = fwdX * normal.y - fwdY * normal.x;
    const rl = Math.sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ) || 1;
    rightX /= rl; rightY /= rl; rightZ /= rl;

    out[offset] = rightX * scale; out[offset + 1] = rightY * scale; out[offset + 2] = rightZ * scale; out[offset + 3] = 0;
    out[offset + 4] = fwdX * scale; out[offset + 5] = fwdY * scale; out[offset + 6] = fwdZ * scale; out[offset + 7] = 0;
    out[offset + 8] = normal.x * scale; out[offset + 9] = normal.y * scale; out[offset + 10] = normal.z * scale; out[offset + 11] = 0;
    out[offset + 12] = cx; out[offset + 13] = cy; out[offset + 14] = cz; out[offset + 15] = 1;
}

function buildSphereModel(out, cx, cy, cz, radius) {
    out[0] = radius; out[1] = 0; out[2] = 0; out[3] = 0;
    out[4] = 0; out[5] = radius; out[6] = 0; out[7] = 0;
    out[8] = 0; out[9] = 0; out[10] = radius; out[11] = 0;
    out[12] = cx; out[13] = cy; out[14] = cz; out[15] = 1;
}

function drawRallyTarget() {
    buildSphereModel(rallyModel,
        rallyTarget.x,
        rallyTarget.y,
        rallyTarget.z + TARGET_SPHERE_RADIUS + 0.45,
        TARGET_SPHERE_RADIUS);
    sys.math.mat4Multiply(rallyMVP, viewProj, rallyModel);
    carProgram.uniformMatrix4('u_mvp', rallyMVP);
    carProgram.uniformMatrix4('u_model', rallyModel);
    carProgram.uniform3f('u_color', RALLY_SPHERE_RGB[0], RALLY_SPHERE_RGB[1], RALLY_SPHERE_RGB[2]);
    carProgram.uniform3f('u_light_dir', LIGHT_DIR[0], LIGHT_DIR[1], LIGHT_DIR[2]);
    carProgram.uniform3f('u_light_color', LIGHT_COLOR[0], LIGHT_COLOR[1], LIGHT_COLOR[2]);
    carProgram.uniform3f('u_eye', eye[0], eye[1], eye[2]);
    carProgram.uniform3f('u_fog_color', FOG_COLOR_RGB[0], FOG_COLOR_RGB[1], FOG_COLOR_RGB[2]);
    carProgram.uniform1f('u_ambient', Math.max(0.28, ambientLight * 0.95));
    carProgram.uniform1f('u_headlight_on', 0.0);
    carProgram.uniform1f('u_fog_start', FOG_START);
    carProgram.uniform1f('u_fog_end', FOG_END);
    carProgram.drawMesh(rallySphereMesh.layout, {
        mode: 'triangles',
        first: 0,
        count: rallySphereMesh.indexCount,
        depthTest: true,
        depthWrite: true,
        cull: 'back',
        blend: 'none',
    });
}

function drawPalms() {
    palmProgram.uniform3f('u_eye', eye[0], eye[1], eye[2]);
    palmProgram.uniform3f('u_fog_color', FOG_COLOR_RGB[0], FOG_COLOR_RGB[1], FOG_COLOR_RGB[2]);
    palmProgram.uniform1f('u_fog_start', FOG_START);
    palmProgram.uniform1f('u_fog_end', FOG_END);
    palmProgram.uniform3f('u_light_dir', LIGHT_DIR[0], LIGHT_DIR[1], LIGHT_DIR[2]);
    palmProgram.uniform3f('u_light_color', LIGHT_COLOR[0], LIGHT_COLOR[1], LIGHT_COLOR[2]);
    palmProgram.uniform1f('u_ambient', ambientLight * 0.82);

    palmDrawList.length = 0;
    const drawRadius2 = PALM_DRAW_RADIUS * PALM_DRAW_RADIUS;

    for (const chunk of activeChunks.values()) {
        if (!isChunkVisible(chunk)) continue;
        const palms = getPalmChunk(chunk.cx, chunk.cy);
        for (const palm of palms) {
            const dx = palm.x - playerCar.x;
            const dy = palm.y - playerCar.y;
            const dist2 = dx * dx + dy * dy;
            if (dist2 <= drawRadius2) palmDrawList.push({ palm, dist2 });
        }
    }

    // Draw only the nearest palms. This keeps the scene under the 64-pass
    // runtime cap while fog hides vegetation culled near the horizon.
    palmDrawList.sort((a, b) => a.dist2 - b.dist2);
    const visibleChunkCount = Math.min(activeChunks.size, (VIEW_RADIUS_CHUNKS * 2 + 1) * (VIEW_RADIUS_CHUNKS * 2 + 1));
    const passBudget = Math.max(0, 58 - visibleChunkCount - cars.length * 2);
    const count = Math.min(palmDrawList.length, PALM_MAX_DRAWS, passBudget);
    for (let i = 0; i < count; i++) {
        const palm = palmDrawList[i].palm;
        const mesh = palmVariants[palm.variant];
        buildTerrainObjectModel(palmModel, palm.x, palm.y, palm.z, palm.rot, palm.scale);
        sys.math.mat4Multiply(palmMVP, viewProj, palmModel);
        palmProgram.uniformMatrix4('u_mvp', palmMVP);
        palmProgram.uniformMatrix4('u_model', palmModel);
        palmProgram.drawMesh(mesh.layout, {
            mode: 'triangles',
            first: 0,
            count: mesh.indexCount,
            depthTest: true,
            depthWrite: true,
            cull: 'none',
            blend: 'none',
        });
    }
    prunePalmChunks();
}

// ============ Frame ============

function frame(timestamp) {
    updateResponsiveLayout();
    const input = sys.input.get();
    const dt = input.deltaTime;
    if (dt > 0) {
        currentFPS = 1 / dt;
        smoothedFPS = smoothedFPS === 0
            ? currentFPS
            : smoothedFPS + (currentFPS - smoothedFPS) * 0.12;
    }
    updateDayNightLighting(timestamp);
    updateRallyTarget(timestamp, dt);

    updateTouchInput(input);
    updateParamKeyboard(dt);
    updateCamera(dt);
    const forces = updatePhysics(playerCar, dt, gatherPlayerControls(), terrainPhysics);
    for (let i = 1; i < cars.length; i++) {
        updatePhysics(cars[i], dt, gatherRallyAIControls(cars[i], dt), terrainPhysics);
    }
    resolvePalmCollisions();
    resolveVehicleCollisions(cars);

    const w = sys.window.getWidth();
    const h = sys.window.getHeight();

    // ----- Skia 2D pass: sky background + HUD -----
    sys.canvas.clear(skyColorARGB);
    drawHUD(forces);
    drawTouchControls();
    drawPhysicsPanel();
    drawAxisIndicatorFlat();
    drawRadar();

    // ----- Build camera matrices -----
    if (isFirstPersonView()) {
        // Cabin cam follows the car's actual visual attitude: terrain-aligned
        // while grounded, inertial pitch/roll while airborne.
        const axes = getCarVisualAxes(playerCar);
        const normal = axes.up;
        const forward = axes.forward;

        const eyeLift = CAR_HEIGHT * physics.vehicleScale * 7;
        const eyeForward = CAR_LENGTH * physics.vehicleScale * 0.15;
        eye[0] = playerCar.x + normal.x * eyeLift + forward.x * eyeForward;
        eye[1] = playerCar.y + normal.y * eyeLift + forward.y * eyeForward;
        eye[2] = playerCar.z + normal.z * eyeLift + forward.z * eyeForward;

        target[0] = eye[0] + forward.x * 5;
        target[1] = eye[1] + forward.y * 5;
        target[2] = eye[2] + forward.z * 5;

        // Use the car's current up so the horizon banks naturally.
        const fpUp = new Float32Array([normal.x, normal.y, normal.z]);
        sys.math.mat4Perspective(proj, Math.PI / 2.3, w / h, 0.05, 200.0);
        sys.math.mat4LookAt(view, eye, target, fpUp);
    } else {
        const tx = playerCar.x, ty = playerCar.y, tz = playerCar.z + CAR_HEIGHT * physics.vehicleScale / 2;
        target[0] = tx; target[1] = ty; target[2] = tz;
        const savedRotation = cameraRotation;
        if (viewMode === VIEW_FOLLOW) cameraRotation = followCameraRotation;
        getCameraPosition(tx, ty, tz, eye);
        cameraRotation = savedRotation;
        sys.math.mat4Perspective(proj, Math.PI / 3, w / h, 0.1, 200.0);
        sys.math.mat4LookAt(view, eye, target, upZ);
    }
    sys.math.mat4Multiply(viewProj, proj, view);

    // ----- Draw terrain (all visible chunks) -----
    updateChunks(playerCar.x, playerCar.y);
    sys.math.mat4Identity(terrainModel);
    sys.math.mat4Multiply(terrainMVP, viewProj, terrainModel);
    terrainProgram.uniformMatrix4('u_mvp', terrainMVP);
    terrainProgram.uniformMatrix4('u_model', terrainModel);
    terrainProgram.uniform1f('u_height_scale', TERRAIN_HEIGHT_SCALE);
    terrainProgram.uniform3f('u_eye', eye[0], eye[1], eye[2]);
    terrainProgram.uniform3f('u_fog_color', FOG_COLOR_RGB[0], FOG_COLOR_RGB[1], FOG_COLOR_RGB[2]);
    terrainProgram.uniform1f('u_fog_start', FOG_START);
    terrainProgram.uniform1f('u_fog_end', FOG_END);
    terrainProgram.uniform3f('u_light_dir', LIGHT_DIR[0], LIGHT_DIR[1], LIGHT_DIR[2]);
    terrainProgram.uniform3f('u_light_color', LIGHT_COLOR[0], LIGHT_COLOR[1], LIGHT_COLOR[2]);
    terrainProgram.uniform1f('u_ambient', ambientLight);
    for (const chunk of activeChunks.values()) {
        if (!isChunkVisible(chunk)) continue;
        terrainProgram.drawMesh(chunk.layout, {
            mode: 'triangles',
            first: 0,
            count: CHUNK_INDEX_COUNT,
            depthTest: true,
            depthWrite: true,
            cull: 'none',
            blend: 'none',
        });
    }

    // ----- Procedural dashed rally road line, snapped to terrain height. -----
    drawRoad(timestamp);

    // ----- Draw deterministic low-poly palms sharing a few GPU meshes. -----
    drawPalms();

    // ----- Optional rally target marker: hidden by default, AI still chases it. -----
    if (showRallyTarget) drawRallyTarget();

    // ----- Soft shadows on the terrain, batched before the cars so the cars
    // ----- always render on top. -----
    const shadowRadius = Math.max(CAR_LENGTH, CAR_WIDTH) * physics.vehicleScale * 0.85;
    let shadowCursor = 0;
    const shadowCarCount = Math.min(cars.length, MAX_SHADOW_CARS);
    for (let i = 0; i < shadowCarCount; i++) {
        const c = cars[i];
        shadowCursor = buildShadowMeshAt(shadowGPU.data, shadowCursor, c.x, c.y, shadowRadius);
    }
    if (shadowCursor > 0) {
        sys.gl.updateBuffer(shadowGPU.vbo, shadowGPU.data, 0);
        shadowGPU.program.uniformMatrix4('u_mvp', viewProj);
        shadowGPU.program.drawMesh(shadowGPU.layout, {
            mode: 'triangles',
            first: 0,
            count: shadowCarCount * SHADOW_INDICES_PER_CAR,
            depthTest: true,
            depthWrite: false,
            cull: 'none',
            blend: 'alpha',
        });
    }

    // ----- Draw cars (skip player car in first-person view; we'd be inside it).
    // ----- All cars share one program: setUniform* before each drawMesh is
    // ----- snapshotted into that pass. -----
    for (let i = 0; i < cars.length; i++) {
        const c = cars[i];
        if (i === 0 && isFirstPersonView()) continue;
        const mesh = c.mesh.gpu;
        buildCarModel(carModel, c);
        sys.math.mat4Multiply(carMVP, viewProj, carModel);
        carProgram.uniformMatrix4('u_mvp', carMVP);
        carProgram.uniformMatrix4('u_model', carModel);
        carProgram.uniform3f('u_color', c.color[0], c.color[1], c.color[2]);
        carProgram.uniform3f('u_light_dir', LIGHT_DIR[0], LIGHT_DIR[1], LIGHT_DIR[2]);
        carProgram.uniform3f('u_light_color', LIGHT_COLOR[0], LIGHT_COLOR[1], LIGHT_COLOR[2]);
        carProgram.uniform3f('u_eye', eye[0], eye[1], eye[2]);
        carProgram.uniform3f('u_fog_color', FOG_COLOR_RGB[0], FOG_COLOR_RGB[1], FOG_COLOR_RGB[2]);
        carProgram.uniform1f('u_ambient', ambientLight * 0.78);
        carProgram.uniform1f('u_headlight_on', headlightNightFactor);
        carProgram.uniform1f('u_fog_start', FOG_START);
        carProgram.uniform1f('u_fog_end', FOG_END);
        carProgram.drawMesh(mesh.layout, {
            mode: 'triangles',
            first: 0,
            count: mesh.indexCount,
            depthTest: true,
            depthWrite: true,
            cull: 'back',
            blend: 'none',
        });
    }

    // ----- Warm headlight beams fade in at night and off during daytime. -----
    drawHeadlights(timestamp);

    // ----- Force arrows as a dynamic line mesh -----
    if (showRallyTarget) {
        lineReset();
        const arrowScale = 0.3;
        const ax = playerCar.x, ay = playerCar.y, az = playerCar.z + CAR_HEIGHT * physics.vehicleScale + 0.2;
        lineArrow(ax, ay, az, forces.gravityForce.x, forces.gravityForce.y, forces.gravityForce.z, arrowScale, 1.0, 0.27, 0.27);
        lineArrow(ax, ay, az, forces.reactionForce.x, forces.reactionForce.y, forces.reactionForce.z, arrowScale, 0.27, 1.0, 0.27);
        const slopeLen = Math.hypot(forces.gravityTangent.x, forces.gravityTangent.y, forces.gravityTangent.z);
        if (slopeLen > 0.1) {
            lineArrow(ax, ay, az, forces.gravityTangent.x, forces.gravityTangent.y, forces.gravityTangent.z, arrowScale, 1.0, 1.0, 0.27);
        }
        const resLen = Math.hypot(forces.resultantForce.x, forces.resultantForce.y, forces.resultantForce.z);
        if (resLen > 0.1) {
            lineArrow(ax, ay, az, forces.resultantForce.x, forces.resultantForce.y, forces.resultantForce.z, arrowScale, 0.27, 0.87, 1.0);
        }
        if (lineCursor > 0) {
            sys.gl.updateBuffer(lineVBO, lineData, 0);
            linesProgram.uniformMatrix4('u_mvp', viewProj);
            linesProgram.drawMesh(lineLayout, {
                mode: 'lines',
                first: 0,
                count: lineCursor / 6, // 6 floats per vertex
                depthTest: true,
                depthWrite: false,
                cull: 'none',
                blend: 'alpha',
            });
        }
    }

    // ----- HUD overlay on top of the mesh (chroma-keys out the sky) -----
    hudOverlayProgram.uniform3f('u_sky_color', SKY_COLOR_RGB[0], SKY_COLOR_RGB[1], SKY_COLOR_RGB[2]);
    sys.gl.bindScreen();
    hudOverlayProgram.drawFullscreen();

    sys.animation.requestFrame(frame);
}

// ============ HUD (Skia 2D) ============

function drawAxisIndicatorFlat() {
    // 2D axis legend in the corner. The 3D world-axis indicator is omitted
    // here because Skia draws are composited beneath the GL mesh pass; a
    // proper indicator would itself need to be a 3D mesh draw.
    const ox = 70, oy = 120;
    sys.canvas.setStrokeWidth(2);
    sys.canvas.setStrokeColor('#FF4444');
    sys.canvas.drawLine(ox, oy, ox + 40, oy);
    sys.canvas.setFillColor('#FF4444');
    sys.canvas.drawText('X', ox + 45, oy + 4, 12);
    sys.canvas.setStrokeColor('#44FF44');
    sys.canvas.drawLine(ox, oy, ox, oy - 40);
    sys.canvas.setFillColor('#44FF44');
    sys.canvas.drawText('Y', ox - 4, oy - 45, 12);
    sys.canvas.setStrokeColor('#4444FF');
    sys.canvas.drawLine(ox, oy, ox - 28, oy + 28);
    sys.canvas.setFillColor('#4444FF');
    sys.canvas.drawText('Z', ox - 40, oy + 36, 12);
}

function drawRadar() {
    // Heading-up minimap: player is fixed in the center, and other cars are
    // rotated into the player's local frame so "ahead" is always upward.
    sys.canvas.setFillColor('#00000090');
    sys.canvas.drawCircle(RADAR_X, RADAR_Y, RADAR_RADIUS + 8);
    sys.canvas.setStrokeColor('#FFFFFF70');
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawCircle(RADAR_X, RADAR_Y, RADAR_RADIUS);
    sys.canvas.setStrokeColor('#FFFFFF30');
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawCircle(RADAR_X, RADAR_Y, RADAR_RADIUS * 0.5);
    sys.canvas.drawLine(RADAR_X - RADAR_RADIUS, RADAR_Y, RADAR_X + RADAR_RADIUS, RADAR_Y);
    sys.canvas.drawLine(RADAR_X, RADAR_Y - RADAR_RADIUS, RADAR_X, RADAR_Y + RADAR_RADIUS);

    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('RADAR', RADAR_X - 24, RADAR_Y - RADAR_RADIUS - 15, 11);
    sys.canvas.setFillColor('#FFFFFFA0');
    sys.canvas.drawText('N', RADAR_X - 4, RADAR_Y - RADAR_RADIUS + 13, 10);

    const cosH = Math.cos(playerCar.heading);
    const sinH = Math.sin(playerCar.heading);
    const scale = RADAR_RADIUS / RADAR_RANGE;

    // Optional rally destination marker.
    if (showRallyTarget) {
        const dx = rallyTarget.x - playerCar.x;
        const dy = rallyTarget.y - playerCar.y;
        const forward = dx * cosH + dy * sinH;
        const right = dx * sinH - dy * cosH;
        const dist = Math.hypot(right, forward);
        const clamped = Math.min(dist, RADAR_RANGE);
        const k = dist > 1e-4 ? (clamped / dist) : 0;
        const bx = RADAR_X + right * k * scale;
        const by = RADAR_Y - forward * k * scale;
        sys.canvas.setFillColor(dist > RADAR_RANGE ? '#DDE6FF' : '#BFC8FF');
        sys.canvas.drawCircle(bx, by, dist > RADAR_RANGE ? 6 : 7);
        sys.canvas.setStrokeColor('#5058FFC0');
        sys.canvas.setStrokeWidth(2);
        sys.canvas.drawCircle(bx, by, dist > RADAR_RANGE ? 6 : 7);
        sys.canvas.setFillColor('#FFFFFFD0');
        sys.canvas.drawText('●', bx - 4, by + 4, 10);
    }

    for (let i = 1; i < cars.length; i++) {
        const car = cars[i];
        const dx = car.x - playerCar.x;
        const dy = car.y - playerCar.y;

        // Local axes: +forward maps to screen up, +right maps to screen right.
        const forward = dx * cosH + dy * sinH;
        const right = dx * sinH - dy * cosH;
        const dist = Math.hypot(right, forward);
        const clamped = Math.min(dist, RADAR_RANGE);
        const k = dist > 1e-4 ? (clamped / dist) : 0;
        const bx = RADAR_X + right * k * scale;
        const by = RADAR_Y - forward * k * scale;
        const edge = dist > RADAR_RANGE;

        sys.canvas.setFillColor(edge ? '#FFCC44' : '#44A6FF');
        sys.canvas.drawCircle(bx, by, edge ? 4 : 5);
        sys.canvas.setStrokeColor('#FFFFFFB0');
        sys.canvas.setStrokeWidth(1);
        sys.canvas.drawCircle(bx, by, edge ? 4 : 5);
    }

    // Player marker: small heading arrow at center.
    sys.canvas.setFillColor('#FF4444');
    sys.path.reset(radarPlayerPath);
    sys.path.moveTo(radarPlayerPath, RADAR_X, RADAR_Y - 8);
    sys.path.lineTo(radarPlayerPath, RADAR_X - 6, RADAR_Y + 6);
    sys.path.lineTo(radarPlayerPath, RADAR_X + 6, RADAR_Y + 6);
    sys.path.close(radarPlayerPath);
    sys.canvas.drawPath(radarPlayerPath);
}

function drawHUD(forces) {
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('TERRAIN PHYSICS DEMO (3D MESH PIPELINE)', 10, 25, 20);

    sys.canvas.setFillColor('#444444');
    sys.canvas.drawText('Keys: W/S throttle, A/D steer, Q/E rotate cam, R/F angle, T/G zoom, V cycle view', 10, 50, 12);

    const fpsX = 10;
    const fpsY = 66;
    sys.canvas.setFillColor('#00000080');
    sys.canvas.drawRoundRect(fpsX - 4, fpsY - 14, 86, 22, 5, 5);
    sys.canvas.setFillColor(smoothedFPS >= 50 ? '#7CFF8B' : smoothedFPS >= 30 ? '#FFE066' : '#FF6666');
    sys.canvas.drawText(`FPS: ${smoothedFPS.toFixed(0)}`, fpsX, fpsY + 2, 14);

    const legendX = screenWidth - 180;
    const legendY = 30;
    sys.canvas.setFillColor('#00000080');
    sys.canvas.drawRoundRect(legendX - 10, legendY - 20, 180, 120, 8, 8);

    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Forces:', legendX, legendY, 14);

    sys.canvas.setFillColor('#FF4444');
    sys.canvas.drawRect(legendX, legendY + 10, 20, 3);
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Gravity', legendX + 30, legendY + 15, 12);

    sys.canvas.setFillColor('#44FF44');
    sys.canvas.drawRect(legendX, legendY + 30, 20, 3);
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Reaction', legendX + 30, legendY + 35, 12);

    sys.canvas.setFillColor('#FFFF44');
    sys.canvas.drawRect(legendX, legendY + 50, 20, 3);
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Slope', legendX + 30, legendY + 55, 12);

    sys.canvas.setFillColor('#44DDFF');
    sys.canvas.drawRect(legendX, legendY + 70, 20, 3);
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Input', legendX + 30, legendY + 75, 12);

    sys.canvas.setFillColor('#222222');
    const terrainZ = getTerrainHeight(playerCar.x, playerCar.y);
    const altitude = playerCar.z - terrainZ;
    sys.canvas.drawText(`Position: (${playerCar.x.toFixed(1)}, ${playerCar.y.toFixed(1)}, ${playerCar.z.toFixed(1)})  alt=${altitude.toFixed(2)}  vz=${playerCar.vz.toFixed(2)}`, 10, screenHeight - 25, 12);
    const shownRot = viewMode === VIEW_FOLLOW ? followCameraRotation : cameraRotation;
    sys.canvas.drawText(`Camera: ${viewModeLabel()} rot=${(shownRot * 180 / Math.PI).toFixed(0)}° ang=${(cameraAngle * 180 / Math.PI).toFixed(0)}° dist=${cameraDistance.toFixed(1)}`, 10, screenHeight - 10, 12);

    if (!playerCar.grounded) {
        sys.canvas.setFillColor('#FFCC44');
        sys.canvas.drawText(`AIRBORNE  ${playerCar.airborneTime.toFixed(2)}s`, screenWidth / 2 - 60, 70, 18);
    }
}

function drawPhysicsPanel() {
    // Background.
    sys.canvas.setFillColor('#000000A0');
    sys.canvas.drawRoundRect(PARAM_PANEL_X - 6, PARAM_PANEL_Y - 22,
        PARAM_PANEL_WIDTH + 12, PARAM_PANEL_HEIGHT, 8, 8);

    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Physics  [ / ] adjust  1-8 select', PARAM_PANEL_X, PARAM_PANEL_Y - 6, 12);

    // Rebuild button hit-rects each frame so layout stays in sync.
    paramButtons.length = 0;

    let y = PARAM_PANEL_Y + 14;
    for (let i = 0; i < physicsParams.length; i++) {
        const def = physicsParams[i];
        const value = physics[def.key];
        const isSelected = (i === selectedParamIndex);

        const labelX = PARAM_PANEL_X;
        const labelW = PARAM_PANEL_WIDTH - PARAM_BTN_SIZE * 2 - 8;
        if (isSelected) {
            sys.canvas.setFillColor('#44DDFF40');
            sys.canvas.drawRoundRect(labelX - 4, y - 12, labelW + 8, PARAM_ROW_HEIGHT - 4, 4, 4);
        }
        sys.canvas.setFillColor(isSelected ? '#FFFFFF' : '#DDDDDD');
        sys.canvas.drawText(`${i + 1}. ${def.label}`, labelX, y, 12);
        sys.canvas.setFillColor('#FFE89A');
        sys.canvas.drawText(value.toFixed(2), labelX + 110, y, 12);
        paramButtons.push({
            kind: 'select', index: i,
            x: labelX - 4, y: y - 12, w: labelW + 8, h: PARAM_ROW_HEIGHT - 4,
        });

        const decX = PARAM_PANEL_X + PARAM_PANEL_WIDTH - PARAM_BTN_SIZE * 2 - 4;
        const incX = PARAM_PANEL_X + PARAM_PANEL_WIDTH - PARAM_BTN_SIZE;
        const btnY = y - 14;

        sys.canvas.setFillColor('#FFFFFF40');
        sys.canvas.drawRoundRect(decX, btnY, PARAM_BTN_SIZE, PARAM_BTN_SIZE, 3, 3);
        sys.canvas.drawRoundRect(incX, btnY, PARAM_BTN_SIZE, PARAM_BTN_SIZE, 3, 3);
        sys.canvas.setFillColor('#FFFFFF');
        sys.canvas.drawText('-', decX + 6, btnY + 14, 14);
        sys.canvas.drawText('+', incX + 5, btnY + 14, 14);

        paramButtons.push({ kind: 'dec', index: i, x: decX, y: btnY, w: PARAM_BTN_SIZE, h: PARAM_BTN_SIZE });
        paramButtons.push({ kind: 'inc', index: i, x: incX, y: btnY, w: PARAM_BTN_SIZE, h: PARAM_BTN_SIZE });

        y += PARAM_ROW_HEIGHT;
    }

    const resetW = 90, resetH = 22;
    const resetX = PARAM_PANEL_X + PARAM_PANEL_WIDTH - resetW;
    const resetY = y;
    sys.canvas.setFillColor('#FFFFFF40');
    sys.canvas.drawRoundRect(resetX, resetY, resetW, resetH, 4, 4);
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('Reset (Bksp)', resetX + 6, resetY + 15, 11);
    paramButtons.push({ kind: 'reset', index: -1, x: resetX, y: resetY, w: resetW, h: resetH });
}

function drawTouchControls() {
    sys.canvas.setFillColor('#FFFFFF30');
    sys.canvas.drawCircle(JOYSTICK_X, JOYSTICK_Y, JOYSTICK_RADIUS);
    sys.canvas.setStrokeColor('#FFFFFF60');
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawCircle(JOYSTICK_X, JOYSTICK_Y, JOYSTICK_RADIUS);

    const thumbX = JOYSTICK_X + touchJoystickX * JOYSTICK_RADIUS * 0.8;
    const thumbY = JOYSTICK_Y + touchJoystickY * JOYSTICK_RADIUS * 0.8;
    const thumbRadius = JOYSTICK_RADIUS * 0.4;
    sys.canvas.setFillColor(touchJoystickActive ? '#44DDFF80' : '#FFFFFF50');
    sys.canvas.drawCircle(thumbX, thumbY, thumbRadius);
    sys.canvas.setStrokeColor('#FFFFFFA0');
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawCircle(thumbX, thumbY, thumbRadius);

    sys.canvas.setFillColor('#FFFFFFA0');
    sys.canvas.drawText('MOVE', JOYSTICK_X - 20, JOYSTICK_Y + JOYSTICK_RADIUS + 20, 12);

    for (const btn of cameraButtons) {
        const isActive = activeButtons.has(btn.action);
        sys.canvas.setFillColor(isActive ? '#44DDFF80' : '#FFFFFF30');
        sys.canvas.drawCircle(btn.x, btn.y, BUTTON_SIZE / 2);
        sys.canvas.setStrokeColor(isActive ? '#44DDFFC0' : '#FFFFFF60');
        sys.canvas.setStrokeWidth(2);
        sys.canvas.drawCircle(btn.x, btn.y, BUTTON_SIZE / 2);
        sys.canvas.setFillColor(isActive ? '#FFFFFF' : '#FFFFFFA0');
        sys.canvas.drawText(btn.label, btn.x - 8, btn.y + 6, 20);
    }

    sys.canvas.setFillColor('#FFFFFFA0');
    sys.canvas.drawText('ROTATE', BUTTON_X - 30, BUTTON_Y_START - 25, 10);
    sys.canvas.drawText('ANGLE', BUTTON_X - 20, BUTTON_Y_START + 80, 10);
    sys.canvas.drawText('ZOOM', BUTTON_X - 18, BUTTON_Y_START + 185, 10);

    // Rally target visibility toggle button.
    const tbx = TARGET_BTN_X - TARGET_BTN_W / 2;
    const tby = TARGET_BTN_Y - TARGET_BTN_H / 2;
    sys.canvas.setFillColor(showRallyTarget ? '#FFE06680' : '#FFFFFF30');
    sys.canvas.drawRoundRect(tbx, tby, TARGET_BTN_W, TARGET_BTN_H, 6, 6);
    sys.canvas.setStrokeColor(showRallyTarget ? '#FFE066C0' : '#FFFFFF60');
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawRoundRect(tbx, tby, TARGET_BTN_W, TARGET_BTN_H, 6, 6);
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText(showRallyTarget ? 'TARGET ON' : 'TARGET OFF',
        tbx + 11, TARGET_BTN_Y + 5, 14);

    // First-person view toggle button.
    const vbx = VIEW_BTN_X - VIEW_BTN_W / 2;
    const vby = VIEW_BTN_Y - VIEW_BTN_H / 2;
    sys.canvas.setFillColor(viewMode !== VIEW_ORBIT ? '#44DDFF80' : '#FFFFFF30');
    sys.canvas.drawRoundRect(vbx, vby, VIEW_BTN_W, VIEW_BTN_H, 6, 6);
    sys.canvas.setStrokeColor(viewMode !== VIEW_ORBIT ? '#44DDFFC0' : '#FFFFFF60');
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawRoundRect(vbx, vby, VIEW_BTN_W, VIEW_BTN_H, 6, 6);
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText(`${viewModeLabel()} (V)`,
        vbx + 10, VIEW_BTN_Y + 5, 14);
}

// Start the application
sys.animation.requestFrame(frame);
