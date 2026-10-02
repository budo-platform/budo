#version 300 es
precision highp float;
in vec3 a_position;
in vec3 a_normal;
in vec3 a_color;

uniform mat4 u_mvp;
uniform mat4 u_model;

out vec3 v_normal;
out vec3 v_color;
out vec3 v_world_pos;

void main() {
    vec4 world = u_model * vec4(a_position, 1.0);
    v_world_pos = world.xyz;
    v_normal = mat3(u_model) * a_normal;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
