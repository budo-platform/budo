#version 300 es
precision highp float;
in vec2 a_position;

// Camera basis, pre-scaled by tan(fov/2) * aspect (right) and tan(fov/2) (up),
// so each corner of the screen quad gets its world-space view ray.
uniform vec3 u_cam_right;
uniform vec3 u_cam_up;
uniform vec3 u_cam_forward;

out vec3 v_dir;

void main() {
    v_dir = u_cam_forward + a_position.x * u_cam_right + a_position.y * u_cam_up;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
