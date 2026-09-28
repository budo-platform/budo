// 3D Mesh Cube — exercises the new sys.gl 3D pipeline.
// - Vertex/index buffers
// - Vertex layout with position/normal/color attributes
// - Matrix uniforms (model, view, projection)
// - Depth test + back-face culling
//
// Inline mat4 helpers (until sys.math ships in a follow-up phase).

const M = {
    identity(out) {
        out[0]=1; out[1]=0; out[2]=0; out[3]=0;
        out[4]=0; out[5]=1; out[6]=0; out[7]=0;
        out[8]=0; out[9]=0; out[10]=1; out[11]=0;
        out[12]=0; out[13]=0; out[14]=0; out[15]=1;
        return out;
    },
    multiply(out, a, b) {
        const a00=a[0],a01=a[1],a02=a[2],a03=a[3];
        const a10=a[4],a11=a[5],a12=a[6],a13=a[7];
        const a20=a[8],a21=a[9],a22=a[10],a23=a[11];
        const a30=a[12],a31=a[13],a32=a[14],a33=a[15];
        for (let i = 0; i < 4; i++) {
            const b0=b[i*4], b1=b[i*4+1], b2=b[i*4+2], b3=b[i*4+3];
            out[i*4]   = b0*a00 + b1*a10 + b2*a20 + b3*a30;
            out[i*4+1] = b0*a01 + b1*a11 + b2*a21 + b3*a31;
            out[i*4+2] = b0*a02 + b1*a12 + b2*a22 + b3*a32;
            out[i*4+3] = b0*a03 + b1*a13 + b2*a23 + b3*a33;
        }
        return out;
    },
    perspective(out, fovYRad, aspect, near, far) {
        const f = 1.0 / Math.tan(fovYRad / 2);
        const nf = 1.0 / (near - far);
        out[0]=f/aspect; out[1]=0; out[2]=0; out[3]=0;
        out[4]=0; out[5]=f; out[6]=0; out[7]=0;
        out[8]=0; out[9]=0; out[10]=(far+near)*nf; out[11]=-1;
        out[12]=0; out[13]=0; out[14]=2*far*near*nf; out[15]=0;
        return out;
    },
    translate(out, x, y, z) {
        M.identity(out);
        out[12]=x; out[13]=y; out[14]=z;
        return out;
    },
    rotateY(out, rad) {
        const s = Math.sin(rad), c = Math.cos(rad);
        M.identity(out);
        out[0]=c; out[2]=-s; out[8]=s; out[10]=c;
        return out;
    },
    rotateX(out, rad) {
        const s = Math.sin(rad), c = Math.cos(rad);
        M.identity(out);
        out[5]=c; out[6]=s; out[9]=-s; out[10]=c;
        return out;
    },
};

// Cube geometry: 8 unique positions are not enough because each face needs its
// own normal + color. So we expand to 24 vertices (4 per face) + 36 indices.
function buildCube() {
    // Faces: +X, -X, +Y, -Y, +Z, -Z
    const faces = [
        { n: [ 1, 0, 0], c: [1.0, 0.3, 0.3], v: [[1,-1,-1],[1, 1,-1],[1, 1, 1],[1,-1, 1]] },
        { n: [-1, 0, 0], c: [0.3, 1.0, 0.3], v: [[-1,-1, 1],[-1, 1, 1],[-1, 1,-1],[-1,-1,-1]] },
        { n: [ 0, 1, 0], c: [0.3, 0.5, 1.0], v: [[-1, 1,-1],[-1, 1, 1],[ 1, 1, 1],[ 1, 1,-1]] },
        { n: [ 0,-1, 0], c: [1.0, 1.0, 0.3], v: [[-1,-1, 1],[-1,-1,-1],[ 1,-1,-1],[ 1,-1, 1]] },
        { n: [ 0, 0, 1], c: [1.0, 0.4, 1.0], v: [[-1,-1, 1],[ 1,-1, 1],[ 1, 1, 1],[-1, 1, 1]] },
        { n: [ 0, 0,-1], c: [0.3, 1.0, 1.0], v: [[ 1,-1,-1],[-1,-1,-1],[-1, 1,-1],[ 1, 1,-1]] },
    ];

    // Interleaved: pos(3) + normal(3) + color(3) = 9 floats per vertex, 24 verts
    const vertices = new Float32Array(24 * 9);
    const indices = new Uint16Array(36);
    let vi = 0, ii = 0;
    for (let f = 0; f < faces.length; f++) {
        const face = faces[f];
        const baseVertex = f * 4;
        for (let k = 0; k < 4; k++) {
            const p = face.v[k];
            vertices[vi++] = p[0]; vertices[vi++] = p[1]; vertices[vi++] = p[2];
            vertices[vi++] = face.n[0]; vertices[vi++] = face.n[1]; vertices[vi++] = face.n[2];
            vertices[vi++] = face.c[0]; vertices[vi++] = face.c[1]; vertices[vi++] = face.c[2];
        }
        // two CCW triangles per face
        indices[ii++] = baseVertex + 0; indices[ii++] = baseVertex + 1; indices[ii++] = baseVertex + 2;
        indices[ii++] = baseVertex + 0; indices[ii++] = baseVertex + 2; indices[ii++] = baseVertex + 3;
    }
    return { vertices, indices };
}

const programId = sys.gl.createProgram('cube.vert', 'cube.frag');
const { vertices, indices } = buildCube();
const vbo = sys.gl.createBuffer('vertex', vertices);
const ibo = sys.gl.createBuffer('index', indices);
const layout = sys.gl.createVertexLayout();

const STRIDE = 9 * 4;  // 9 floats * 4 bytes
const locPos    = sys.gl.getAttribLocation(programId, 'a_position');
const locNormal = sys.gl.getAttribLocation(programId, 'a_normal');
const locColor  = sys.gl.getAttribLocation(programId, 'a_color');
sys.gl.setAttribute(layout, locPos,    vbo, 3, 'float', false, STRIDE, 0);
sys.gl.setAttribute(layout, locNormal, vbo, 3, 'float', false, STRIDE, 3 * 4);
sys.gl.setAttribute(layout, locColor,  vbo, 3, 'float', false, STRIDE, 6 * 4);
sys.gl.setIndexBuffer(layout, ibo, 'u16');

const proj  = new Float32Array(16);
const view  = new Float32Array(16);
const model = new Float32Array(16);
const tmp   = new Float32Array(16);
const mvp   = new Float32Array(16);
const ry    = new Float32Array(16);
const rx    = new Float32Array(16);

function frame(timestamp) {
    const w = sys.window.getWidth();
    const h = sys.window.getHeight();
    const t = timestamp / 1000.0;

    sys.canvas.clear('#101020');
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawText('3D Mesh Cube', 20, 32, 22);
    sys.canvas.setFillColor('#888888');
    sys.canvas.drawText('Built-in vertex/index buffers + matrix uniforms', 20, 56, 14);

    M.perspective(proj, Math.PI / 3, w / h, 0.1, 100.0);
    M.translate(view, 0, 0, -5);

    M.rotateY(ry, t * 0.7);
    M.rotateX(rx, t * 0.4);
    M.multiply(model, ry, rx);

    M.multiply(tmp, view, model);
    M.multiply(mvp, proj, tmp);

    sys.gl.setUniformMatrix4(programId, 'u_mvp', mvp);
    sys.gl.setUniformMatrix4(programId, 'u_model', model);

    sys.gl.drawMesh(programId, layout, {
        mode: 'triangles',
        first: 0,
        count: 36,
        depthTest: true,
        depthWrite: true,
        cull: 'back',
        blend: 'none',
    });

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
