#version 300 es
precision highp float;
out vec4 fragColor;
in vec3 v_local;

void main() {
    vec3 c = v_local * 0.5 + 0.5;
    fragColor = vec4(c, 1.0);
}
