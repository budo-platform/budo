#version 300 es
precision highp float;
out vec4 fragColor;
in vec2 v_uv;

void main() {
    // v_uv is centered: (0,0) at the shadow center, length 1 at the rim.
    float r = length(v_uv);
    if (r > 1.0) discard;
    // Soft falloff: dense center, fading to fully transparent at the rim.
    float a = (1.0 - r * r) * 0.55;
    fragColor = vec4(0.0, 0.0, 0.0, a);
}
