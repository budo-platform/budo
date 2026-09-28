// 3D Mesh from OBJ — exercises Mesh.fromOBJ + Mesh.upload + Lambert lighting.
// The icosahedron has no `vn`/`vt`, so Mesh.fromOBJ generates flat normals.
/// <reference path="../../budo.d.ts" />

import { Mesh } from './mesh.js';

const program = sys.gl.createProgram('mesh.vert', 'mesh.frag')

const objText = sys.assets.readText('icosahedron.obj');
const mesh = Mesh.fromOBJ(objText);
const { layout, indexCount } = Mesh.upload(mesh);

const proj = new Float32Array(16);
const view = new Float32Array(16);
const model = new Float32Array(16);
const tmp = new Float32Array(16);
const mvp = new Float32Array(16);

const eye = new Float32Array([0, 0, 6]);
const target = new Float32Array([0, 0, 0]);
const up = new Float32Array([0, 1, 0]);

function frame(timestamp) {
    const w = sys.window.getWidth();
    const h = sys.window.getHeight();
    const t = timestamp / 1000.0;

    sys.canvas.clear('#0a0a18');
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('OBJ → Mesh.fromOBJ → drawMesh', 20, 32, 22);
    sys.canvas.setFillColor('#888888');
    sys.canvas.drawText(`Loaded ${mesh.positions.length / 3} verts, ${indexCount / 3} tris`, 20, 56, 14);

    sys.math.mat4Perspective(proj, Math.PI / 3, w / h, 0.1, 100.0);
    sys.math.mat4LookAt(view, eye, target, up);

    sys.math.mat4Identity(model);
    sys.math.mat4RotateY(model, model, t * 0.6);
    sys.math.mat4RotateX(model, model, t * 0.3);

    sys.math.mat4Multiply(tmp, view, model);
    sys.math.mat4Multiply(mvp, proj, tmp);

    sys.gl.setUniformMatrix4(program, 'u_mvp', mvp);
    sys.gl.setUniformMatrix4(program, 'u_model', model);

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
