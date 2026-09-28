#version 300 es
precision highp float;
in vec3 a_position;
in vec2 a_uv;
in vec3 a_normal;

uniform mat4 u_mvp;
uniform mat4 u_model;

out vec2 v_uv;
out vec3 v_normal;

void main() {
    v_uv = a_uv;
    v_normal = mat3(u_model) * a_normal;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
