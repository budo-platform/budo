#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;   // the HUD, drawn on a transparent canvas
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform vec2 u_resolution;
uniform float u_time;
uniform float u_scene_flip;   // 1: the scene target is stored bottom-up
uniform float u_bloom_strength;
uniform float u_horizon;      // screen v of the horizon line (0 = top)
uniform float u_haze;         // heat shimmer strength
uniform vec3 u_lift;          // color grade
uniform vec3 u_gain;
uniform float u_saturation;
uniform float u_contrast;
uniform float u_speed_fx;
uniform float u_vignette;

in vec2 v_texCoord;

float hash(float n) { return fract(sin(n) * 43758.5453); }

vec2 sceneUV(vec2 uv) {
    return vec2(uv.x, mix(uv.y, 1.0 - uv.y, u_scene_flip));
}

void main() {
    vec2 uv = v_texCoord;

    // Heat shimmer: a thin wavy band hugging the horizon.
    float band = exp(-abs(uv.y - u_horizon) * 14.0) * step(u_horizon - 0.04, uv.y);
    float wave = sin(uv.y * 260.0 + u_time * 7.0) * 0.6 + sin(uv.y * 97.0 - u_time * 3.3 + uv.x * 9.0) * 0.4;
    vec2 hazeUV = uv + vec2(wave * 0.0016, wave * 0.0006) * band * u_haze;

    vec3 c = texture(u_scene, sceneUV(hazeUV)).rgb;
    c += texture(u_bloom, uv).rgb * u_bloom_strength;

    // Grade: gain and lift, a gentle S-curve, saturation.
    c = c * u_gain + u_lift * (1.0 - c);
    c = clamp(c, 0.0, 1.0);
    c = mix(c, c * c * (3.0 - 2.0 * c), u_contrast);
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, u_saturation);

    // Vignette and speed lines, as in hud_overlay.frag.
    vec2 p = uv * 2.0 - 1.0;
    p.x *= u_resolution.x / u_resolution.y;
    float r = length(p);
    c *= 1.0 - smoothstep(0.75, 1.9, r) * (u_vignette + u_speed_fx * 0.25);
    if (u_speed_fx > 0.01) {
        float angle = atan(p.y, p.x);
        float lane = floor(angle * 38.0);
        float seed = hash(lane);
        float laneFrac = fract(angle * 38.0);
        float thin = smoothstep(0.5, 0.0, abs(laneFrac - 0.5) * (2.0 + seed * 4.0));
        float run = fract(r * 0.8 - u_time * (1.6 + seed * 1.4) + seed * 7.0);
        float streak = smoothstep(0.0, 0.08, run) * smoothstep(0.55, 0.2, run);
        float s = thin * streak * smoothstep(0.55, 1.25, r) * step(0.6, seed) * u_speed_fx * 0.42;
        c = mix(c, vec3(1.0, 0.95, 0.85), s);
    }

    // The HUD (premultiplied) on top.
    vec4 hud = texture(u_canvas, uv);
    fragColor = vec4(hud.rgb + c * (1.0 - hud.a), 1.0);
}
