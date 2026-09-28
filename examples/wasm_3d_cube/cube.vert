#version 300 es
precision highp float;
in vec3 a_position;

uniform mat4 u_mvp;

out vec3 v_local;

void main() {
    v_local = a_position;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
