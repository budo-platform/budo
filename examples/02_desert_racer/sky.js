// Procedural sky: gradient, sunset glow, sun, moon, stars, and thin clouds,
// all from the day/night cycle colors. One fullscreen quad drawn before the
// terrain, without depth, so the 3D scene draws over it.

const program = sys.gl.createProgram('sky.vert', 'sky.frag');
const quad = sys.gl.createBuffer('vertex', new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]));
const layout = sys.gl.createVertexLayout();
sys.gl.setAttribute(layout, 0 /* a_position */, quad, 2, 'float', false, 0, 0);

let cloudTime = 0;

// view: the camera's column-major view matrix; fovY in radians.
// sky: { zenith, horizon, glow, sunDir, sunColor, moonDir, night, starAngle,
//        cloudOctaves }.
export function drawSky(view, fovY, aspect, dt, sky) {
    cloudTime += dt * 0.012;
    const tanY = Math.tan(fovY / 2);
    const tanX = tanY * aspect;
    // Rows of the view matrix are the camera axes in world space; the camera
    // looks down its -Z.
    sys.gl.setUniform3f(program, 'u_cam_right', view[0] * tanX, view[4] * tanX, view[8] * tanX);
    sys.gl.setUniform3f(program, 'u_cam_up', view[1] * tanY, view[5] * tanY, view[9] * tanY);
    sys.gl.setUniform3f(program, 'u_cam_forward', -view[2], -view[6], -view[10]);
    sys.gl.setUniform3fv(program, 'u_zenith', sky.zenith);
    sys.gl.setUniform3fv(program, 'u_horizon', sky.horizon);
    sys.gl.setUniform3fv(program, 'u_sky_glow', sky.glow);
    sys.gl.setUniform3fv(program, 'u_sun_dir', sky.sunDir);
    sys.gl.setUniform3fv(program, 'u_sun_color', sky.sunColor);
    sys.gl.setUniform3fv(program, 'u_moon_dir', sky.moonDir);
    sys.gl.setUniform1f(program, 'u_night', sky.night);
    sys.gl.setUniform1f(program, 'u_star_angle', sky.starAngle);
    sys.gl.setUniform1f(program, 'u_cloud_time', cloudTime);
    sys.gl.setUniform1i(program, 'u_cloud_octaves', sky.cloudOctaves);
    sys.gl.drawMesh(program, layout, {
        mode: 'triangle_strip',
        first: 0,
        count: 4,
        depthTest: false,
        depthWrite: false,
        cull: 'none',
        blend: 'none',
    });
}
