// 3D Textured Cube — exercises sys.gl 2D texture upload, sampler binding,
// and the standard a_position/a_uv/a_normal attribute layout used by mesh.js.
//
// Builds the cube with `Mesh.cube()` and uploads a procedural 64×64 checker
// pattern via `sys.gl.createTexture2D`.

import { Mesh } from './mesh.js';

function buildChecker(size, cells) {
    const pixels = new Uint8Array(size * size * 4);
    const cell = size / cells;
    for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
            const cx = (x / cell) | 0;
            const cy = (y / cell) | 0;
            const on = ((cx + cy) & 1) === 0;
            const r = on ? 240 : 32;
            const g = on ? 200 : 80;
            const b = on ? 80 : 220;
            const i = (y * size + x) * 4;
            pixels[i] = r; pixels[i + 1] = g; pixels[i + 2] = b; pixels[i + 3] = 255;
        }
    }
    return pixels;
}

const program = sys.gl.createProgram('cube.vert', 'cube.frag');
const tex = sys.gl.createTexture2D(64, 64, 'rgba8', buildChecker(64, 8));

const cube = Mesh.cube(1.0);
const { layout, indexCount } = Mesh.upload(cube);

const proj  = new Float32Array(16);
const view  = new Float32Array(16);
const model = new Float32Array(16);
const tmp   = new Float32Array(16);
const mvp   = new Float32Array(16);
const eye   = new Float32Array([0, 0, 4]);
const at    = new Float32Array([0, 0, 0]);
const up    = new Float32Array([0, 1, 0]);

function frame(timestamp) {
    const w = sys.window.getWidth();
    const h = sys.window.getHeight();
    const t = timestamp / 1000.0;

    sys.canvas.clear('#101020');
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('3D Textured Cube', 20, 32, 22);
    sys.canvas.setFillColor('#888888');
    sys.canvas.drawText('Mesh.cube + sys.gl.createTexture2D + bindTexture2D', 20, 56, 14);

    sys.math.mat4Perspective(proj, Math.PI / 3, w / h, 0.1, 100.0);
    sys.math.mat4LookAt(view, eye, at, up);

    sys.math.mat4Identity(model);
    sys.math.mat4RotateY(model, model, t * 0.7);
    sys.math.mat4RotateX(model, model, t * 0.4);

    sys.math.mat4Multiply(tmp, view, model);
    sys.math.mat4Multiply(mvp, proj, tmp);

    sys.gl.setUniformMatrix4(program, 'u_mvp', mvp);
    sys.gl.setUniformMatrix4(program, 'u_model', model);
    sys.gl.texture(program, 'u_albedo', tex, 1);

    sys.gl.drawMesh(program, layout, {
        mode: 'triangles',
        first: 0,
        count: indexCount,
        depthTest: true,
        depthWrite: true,
        cull: 'back',
        blend: 'none',
    });

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
