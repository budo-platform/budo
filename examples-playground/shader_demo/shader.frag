#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform vec2 u_mouse;
uniform float u_time;
uniform float u_strength;

in vec2 v_texCoord;

void main() {
    vec2 uv = v_texCoord;
    vec2 mouse = u_mouse / max(u_resolution, vec2(1.0));
    float distanceToMouse = distance(uv, mouse);
    float ripple = sin((distanceToMouse * 30.0) - (u_time * 3.0)) * u_strength;
    vec2 warpedUv = uv + vec2(ripple * 0.6, ripple * 0.3);
    vec4 base = texture(u_canvas, warpedUv);
    vec3 tint = vec3(1.0, 0.98, 0.92) + vec3(0.08, -0.02, -0.04) * sin(u_time + uv.xyx * 6.0);
    fragColor = vec4(base.rgb * tint, base.a);
}