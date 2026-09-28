#version 300 es
precision highp float;
in vec3 a_position;
in vec3 a_normal;

uniform mat4 u_mvp;
uniform mat4 u_model;

out vec3 v_normal;
out vec3 v_local;
out vec3 v_world_pos;

void main() {
    vec4 world = u_model * vec4(a_position, 1.0);
    v_normal = mat3(u_model) * a_normal;
    v_local  = a_position; // local-space position for face tinting
    v_world_pos = world.xyz;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
