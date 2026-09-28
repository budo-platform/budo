#version 300 es
precision highp float;
out vec4 fragColor;
// Amstrad CPC6128 CRT shader.
// Adds barrel curvature, scanlines, slot-mask, glow, chromatic aberration,
// flicker and a strong vignette to mimic an old colour TV / CTM monitor.

uniform sampler2D u_canvas;
uniform vec2  u_resolution;
uniform float u_time;
uniform float u_scanline_intensity;
uniform float u_aberration;
uniform float u_curvature;
uniform float u_glow;

in vec2 v_texCoord;

vec3 sample_glow(vec2 uv, float spread) {
    vec3 c = vec3(0.0);
    c += texture(u_canvas, uv + vec2( spread, 0.0)).rgb;
    c += texture(u_canvas, uv + vec2(-spread, 0.0)).rgb;
    c += texture(u_canvas, uv + vec2(0.0,  spread)).rgb;
    c += texture(u_canvas, uv + vec2(0.0, -spread)).rgb;
    c += texture(u_canvas, uv + vec2( spread,  spread)).rgb;
    c += texture(u_canvas, uv + vec2(-spread,  spread)).rgb;
    c += texture(u_canvas, uv + vec2( spread, -spread)).rgb;
    c += texture(u_canvas, uv + vec2(-spread, -spread)).rgb;
    return c / 8.0;
}

void main() {
    vec2 uv = v_texCoord;

    // Barrel distortion (CRT curvature)
    vec2 centered = uv - 0.5;
    float r2 = dot(centered, centered);
    vec2 warped = uv + centered * r2 * u_curvature;

    // Chromatic aberration: split RGB
    float offset = u_aberration * 0.0035;
    float r = texture(u_canvas, warped + vec2(offset, 0.0)).r;
    float g = texture(u_canvas, warped).g;
    float b = texture(u_canvas, warped - vec2(offset, 0.0)).b;
    vec3 color = vec3(r, g, b);

    // Phosphor glow / bloom
    vec3 glow = sample_glow(warped, 0.0035);
    color += glow * u_glow;

    // Scanlines (horizontal lines)
    float scan = sin(warped.y * u_resolution.y * 1.6) * 0.5 + 0.5;
    scan = mix(1.0, scan, u_scanline_intensity);
    color *= scan;

    // RGB slot mask (subpixel triads)
    float slot = mod(warped.x * u_resolution.x, 3.0);
    vec3 mask = vec3(1.0);
    if (slot < 1.0)      mask = vec3(1.10, 0.85, 0.85);
    else if (slot < 2.0) mask = vec3(0.85, 1.10, 0.85);
    else                 mask = vec3(0.85, 0.85, 1.10);
    color *= mask;

    // Mains hum / flicker
    float flicker = 0.96 + 0.04 * sin(u_time * 9.0);
    color *= flicker;

    // Vignette
    float vignette = 1.0 - r2 * 1.6;
    vignette = clamp(vignette, 0.0, 1.0);
    color *= vignette;

    // Slight warm CRT tint
    color *= vec3(1.02, 1.0, 0.95);

    // Black outside warped area (CRT bezel)
    float m = step(0.0, warped.x) * step(warped.x, 1.0)
            * step(0.0, warped.y) * step(warped.y, 1.0);

    fragColor = vec4(color * m, 1.0);
}
