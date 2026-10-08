#version 300 es
precision highp float;
in vec3 a_position;   // particle center
in vec2 a_uv;         // billboard corner, -1..1
in vec3 a_color;      // size, alpha, seed (the runtime's standard names)

uniform mat4 u_mvp;
uniform vec3 u_cam_right;
uniform vec3 u_cam_up;

out vec2 v_corner;
out float v_alpha;
out float v_seed;
out vec3 v_world_pos;

void main() {
    // Each puff is turned by its own angle so the lumps never line up.
    float angle = a_color.z * 6.2831;
    float c = cos(angle), s = sin(angle);
    vec2 corner = vec2(c * a_uv.x - s * a_uv.y, s * a_uv.x + c * a_uv.y);
    vec3 world = a_position + (u_cam_right * corner.x + u_cam_up * corner.y) * a_color.x;
    v_corner = a_uv;
    v_alpha = a_color.y;
    v_seed = a_color.z;
    v_world_pos = world;
    gl_Position = u_mvp * vec4(world, 1.0);
}
