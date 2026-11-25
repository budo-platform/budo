#version 300 es
precision highp float;
uniform sampler2D u_canvas;
uniform sampler2D u_scene;
uniform vec2 u_texel;
uniform vec4 u_fx; // aberration, flash, glitch, time
in vec2 v_uv;
out vec4 fragColor;

float hash(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

vec3 lightAt(vec2 uv) {
    vec3 c = texture(u_scene, clamp(uv, 0.0, 1.0)).rgb;
    return c * smoothstep(0.32, 0.88, max(c.r, max(c.g, c.b)));
}

void main() {
    float slice = floor(v_uv.y * 46.0);
    float tear = step(0.84, hash(slice * 1.7 + floor(u_fx.w * 13.0)));
    float shift = (hash(slice + floor(u_fx.w * 24.0)) - 0.5) * 0.09 * u_fx.z * tear;
    vec2 uv = vec2(v_uv.x + shift, 1.0 - v_uv.y);
    vec2 split = (uv - 0.5) * u_fx.x * 0.016 + vec2(u_fx.z * tear * 0.006, 0.0);
    vec3 scene = vec3(texture(u_scene, uv + split).r, texture(u_scene, uv).g,
                      texture(u_scene, uv - split).b);
    vec3 glow = lightAt(uv) * 0.21;
    for (int i = 0; i < 8; ++i) {
        float a = float(i) * 0.785398;
        vec2 d = vec2(cos(a), sin(a)) * u_texel;
        glow += lightAt(uv + d * 2.4) * 0.098;
        glow += lightAt(uv + d * 6.5) * 0.060;
        glow += lightAt(uv + d * 13.0) * 0.033;
    }
    vec3 lit = 1.0 - (1.0 - scene) * exp(-glow * (1.55 + 1.4 * u_fx.y));
    lit += vec3(0.16, 0.48, 1.0) * u_fx.y * (0.18 + 0.5 * length(uv - 0.5));
    lit *= 1.0 - 0.22 * u_fx.z * step(0.5, fract(gl_FragCoord.y * 0.5));
    vec4 overlay = texture(u_canvas, vec2(v_uv.x + shift * 0.5, v_uv.y));
    fragColor = vec4(overlay.rgb + lit * (1.0 - overlay.a), 1.0);
}
