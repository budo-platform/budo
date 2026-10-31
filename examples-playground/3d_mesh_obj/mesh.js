/**
 * Mesh helpers for Budo sys.gl 3D pipeline.
 *
 * Geometry generators return a plain object:
 *   {
 *     positions: Float32Array,  // 3 floats per vertex
 *     normals:   Float32Array,  // 3 floats per vertex
 *     uvs:       Float32Array,  // 2 floats per vertex
 *     indices:   Uint16Array | Uint32Array,
 *   }
 *
 * Mesh.upload(mesh) returns:
 *   {
 *     layout:     <vertex layout id>,
 *     indexCount: number,
 *     indexType:  'u16' | 'u32',
 *     buffers:    { position, normal, uv, index }   // for cleanup
 *   }
 *
 * Standard attribute locations (must match shader `layout(location=N)`):
 *   0 = a_position (vec3)
 *   1 = a_uv       (vec2)
 *   2 = a_normal   (vec3)
 */

const ATTR_POSITION = 0;
const ATTR_UV = 1;
const ATTR_NORMAL = 2;

export const Mesh = {
    /**
     * Axis-aligned cube of edge length `size`, centered at the origin.
     * 24 vertices (4 per face) so each face has its own normal & UVs.
     */
    cube(size = 1) {
        const h = size * 0.5;
        // Per face: 4 positions (CCW when viewed from outside), one normal, UVs.
        const faces = [
            { n: [ 1, 0, 0], v: [[ h,-h,-h],[ h, h,-h],[ h, h, h],[ h,-h, h]] }, // +X
            { n: [-1, 0, 0], v: [[-h,-h, h],[-h, h, h],[-h, h,-h],[-h,-h,-h]] }, // -X
            { n: [ 0, 1, 0], v: [[-h, h,-h],[-h, h, h],[ h, h, h],[ h, h,-h]] }, // +Y
            { n: [ 0,-1, 0], v: [[-h,-h, h],[-h,-h,-h],[ h,-h,-h],[ h,-h, h]] }, // -Y
            { n: [ 0, 0, 1], v: [[-h,-h, h],[ h,-h, h],[ h, h, h],[-h, h, h]] }, // +Z
            { n: [ 0, 0,-1], v: [[ h,-h,-h],[-h,-h,-h],[-h, h,-h],[ h, h,-h]] }, // -Z
        ];
        const uvQuad = [[0, 0], [1, 0], [1, 1], [0, 1]];
        const positions = new Float32Array(24 * 3);
        const normals = new Float32Array(24 * 3);
        const uvs = new Float32Array(24 * 2);
        const indices = new Uint16Array(36);
        let pi = 0, ni = 0, ui = 0, ii = 0;
        for (let f = 0; f < 6; f++) {
            const base = f * 4;
            const face = faces[f];
            for (let k = 0; k < 4; k++) {
                positions[pi++] = face.v[k][0];
                positions[pi++] = face.v[k][1];
                positions[pi++] = face.v[k][2];
                normals[ni++] = face.n[0];
                normals[ni++] = face.n[1];
                normals[ni++] = face.n[2];
                uvs[ui++] = uvQuad[k][0];
                uvs[ui++] = uvQuad[k][1];
            }
            indices[ii++] = base + 0; indices[ii++] = base + 1; indices[ii++] = base + 2;
            indices[ii++] = base + 0; indices[ii++] = base + 2; indices[ii++] = base + 3;
        }
        return { positions, normals, uvs, indices };
    },

    /**
     * UV sphere of `radius` with `segments` longitudinal divisions
     * (latitudinal = segments/2).
     */
    sphere(radius = 1, segments = 24) {
        const lon = Math.max(3, segments | 0);
        const lat = Math.max(2, (segments / 2) | 0);
        const vertCount = (lon + 1) * (lat + 1);
        const positions = new Float32Array(vertCount * 3);
        const normals = new Float32Array(vertCount * 3);
        const uvs = new Float32Array(vertCount * 2);
        let p = 0, n = 0, u = 0;
        for (let j = 0; j <= lat; j++) {
            const v = j / lat;
            const theta = v * Math.PI;
            const sinT = Math.sin(theta), cosT = Math.cos(theta);
            for (let i = 0; i <= lon; i++) {
                const uu = i / lon;
                const phi = uu * Math.PI * 2;
                const x = sinT * Math.cos(phi);
                const y = cosT;
                const z = sinT * Math.sin(phi);
                positions[p++] = x * radius;
                positions[p++] = y * radius;
                positions[p++] = z * radius;
                normals[n++] = x; normals[n++] = y; normals[n++] = z;
                uvs[u++] = uu; uvs[u++] = 1.0 - v;
            }
        }
        const triCount = lon * lat * 2;
        const useU32 = vertCount > 65535;
        const indices = useU32 ? new Uint32Array(triCount * 3)
                                : new Uint16Array(triCount * 3);
        let k = 0;
        for (let j = 0; j < lat; j++) {
            for (let i = 0; i < lon; i++) {
                const a = j * (lon + 1) + i;
                const b = a + lon + 1;
                indices[k++] = a;     indices[k++] = b;     indices[k++] = a + 1;
                indices[k++] = b;     indices[k++] = b + 1; indices[k++] = a + 1;
            }
        }
        return { positions, normals, uvs, indices };
    },

    /**
     * Flat plane in the XZ plane (normal = +Y), centered at the origin,
     * size `width` × `height`.
     */
    plane(width = 1, height = 1) {
        const w = width * 0.5, h = height * 0.5;
        const positions = new Float32Array([
            -w, 0, -h,
             w, 0, -h,
             w, 0,  h,
            -w, 0,  h,
        ]);
        const normals = new Float32Array([
            0, 1, 0,  0, 1, 0,  0, 1, 0,  0, 1, 0,
        ]);
        const uvs = new Float32Array([
            0, 0,  1, 0,  1, 1,  0, 1,
        ]);
        const indices = new Uint16Array([0, 1, 2, 0, 2, 3]);
        return { positions, normals, uvs, indices };
    },

    /**
     * Minimal Wavefront OBJ parser: handles `v`, `vn`, `vt`, and `f`
     * (triangles + quads, with optional `v/vt/vn` indices). No materials,
     * no smoothing groups, no negative indices.
     */
    fromOBJ(text) {
        const srcPositions = [], srcNormals = [], srcUVs = [];
        const positions = [], normals = [], uvs = [];
        const indices = [];
        const cache = new Map();

        const lines = text.split('\n');
        for (let li = 0; li < lines.length; li++) {
            const line = lines[li].trim();
            if (!line || line[0] === '#') continue;
            const parts = line.split(/\s+/);
            const tag = parts[0];
            if (tag === 'v') {
                srcPositions.push(+parts[1], +parts[2], +parts[3]);
            } else if (tag === 'vn') {
                srcNormals.push(+parts[1], +parts[2], +parts[3]);
            } else if (tag === 'vt') {
                srcUVs.push(+parts[1], +parts[2]);
            } else if (tag === 'f') {
                const faceVerts = [];
                for (let i = 1; i < parts.length; i++) {
                    const key = parts[i];
                    let idx = cache.get(key);
                    if (idx === undefined) {
                        const s = key.split('/');
                        const pi = (parseInt(s[0], 10) - 1) * 3;
                        positions.push(srcPositions[pi], srcPositions[pi+1], srcPositions[pi+2]);
                        if (s[1]) {
                            const ti = (parseInt(s[1], 10) - 1) * 2;
                            uvs.push(srcUVs[ti] || 0, srcUVs[ti+1] || 0);
                        } else {
                            uvs.push(0, 0);
                        }
                        if (s[2]) {
                            const ni = (parseInt(s[2], 10) - 1) * 3;
                            normals.push(srcNormals[ni], srcNormals[ni+1], srcNormals[ni+2]);
                        } else {
                            normals.push(0, 0, 0);
                        }
                        idx = (positions.length / 3) - 1;
                        cache.set(key, idx);
                    }
                    faceVerts.push(idx);
                }
                // Triangulate as a fan: (0, i, i+1)
                for (let i = 1; i < faceVerts.length - 1; i++) {
                    indices.push(faceVerts[0], faceVerts[i], faceVerts[i + 1]);
                }
            }
        }

        // If the file had no normals, generate flat per-face normals.
        if (srcNormals.length === 0) {
            const n = computeFlatNormals(positions, indices);
            for (let i = 0; i < n.length; i++) normals[i] = n[i];
        }

        const vertCount = positions.length / 3;
        const useU32 = vertCount > 65535;
        return {
            positions: new Float32Array(positions),
            normals:   new Float32Array(normals),
            uvs:       new Float32Array(uvs),
            indices:   useU32 ? new Uint32Array(indices) : new Uint16Array(indices),
        };
    },

    /**
     * Upload a mesh (returned by cube/sphere/plane/fromOBJ) into GL buffers
     * and build a vertex layout binding the standard attribute locations.
     * Returns { layout, indexCount, indexType, buffers }.
     */
    upload(mesh) {
        const indexType = (mesh.indices instanceof Uint32Array) ? 'u32' : 'u16';
        const posBuf = sys.gl.createBuffer('vertex', mesh.positions);
        const uvBuf  = sys.gl.createBuffer('vertex', mesh.uvs);
        const nrmBuf = sys.gl.createBuffer('vertex', mesh.normals);
        const idxBuf = sys.gl.createBuffer('index', mesh.indices);
        const layout = sys.gl.createVertexLayout();
        sys.gl.setAttribute(layout, ATTR_POSITION, posBuf, 3, 'float', false, 0, 0);
        sys.gl.setAttribute(layout, ATTR_UV,       uvBuf,  2, 'float', false, 0, 0);
        sys.gl.setAttribute(layout, ATTR_NORMAL,   nrmBuf, 3, 'float', false, 0, 0);
        sys.gl.setIndexBuffer(layout, idxBuf, indexType);
        return {
            layout,
            indexCount: mesh.indices.length,
            indexType,
            buffers: { position: posBuf, uv: uvBuf, normal: nrmBuf, index: idxBuf },
        };
    },
};

function computeFlatNormals(positions, indices) {
    const out = new Array(positions.length).fill(0);
    for (let i = 0; i < indices.length; i += 3) {
        const a = indices[i] * 3, b = indices[i + 1] * 3, c = indices[i + 2] * 3;
        const ux = positions[b] - positions[a];
        const uy = positions[b + 1] - positions[a + 1];
        const uz = positions[b + 2] - positions[a + 2];
        const vx = positions[c] - positions[a];
        const vy = positions[c + 1] - positions[a + 1];
        const vz = positions[c + 2] - positions[a + 2];
        const nx = uy * vz - uz * vy;
        const ny = uz * vx - ux * vz;
        const nz = ux * vy - uy * vx;
        const len = Math.sqrt(nx*nx + ny*ny + nz*nz) || 1;
        const fx = nx / len, fy = ny / len, fz = nz / len;
        out[a] += fx; out[a+1] += fy; out[a+2] += fz;
        out[b] += fx; out[b+1] += fy; out[b+2] += fz;
        out[c] += fx; out[c+1] += fy; out[c+2] += fz;
    }
    // Re-normalize accumulated per-vertex contributions.
    for (let i = 0; i < out.length; i += 3) {
        const x = out[i], y = out[i + 1], z = out[i + 2];
        const len = Math.sqrt(x*x + y*y + z*z) || 1;
        out[i] = x / len; out[i + 1] = y / len; out[i + 2] = z / len;
    }
    return out;
}

export default Mesh;
