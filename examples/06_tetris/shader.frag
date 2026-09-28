#version 300 es
precision highp float;
out vec4 fragColor;
// Subtle post-processing for Tetris:
//  - cheap pseudo-bloom (bright pixels glow softly)
//  - tiny chromatic aberration toward edges
//  - faint scanlines
//  - gentle vignette
//  - slight CRT-like barrel warp
//  - flash boost driven by u_flash for line clears

uniform sampler2D u_canvas;
uniform vec2  u_resolution;
uniform float u_time;
uniform float u_flash;       // 0..1, pulses on line clear

in vec2 v_texCoord;

vec3 sampleCanvas(vec2 uv) {
    return texture(u_canvas, clamp(uv, 0.001, 0.999)).rgb;
}

vec3 bloom(vec2 uv) {
    // 9-tap blur of bright regions (cheap kawase-ish).
    vec2 px = 1.5 / u_resolution;
    vec3 sum = vec3(0.0);
    sum += sampleCanvas(uv + vec2(-1.0, -1.0) * px);
    sum += sampleCanvas(uv + vec2( 1.0, -1.0) * px);
    sum += sampleCanvas(uv + vec2(-1.0,  1.0) * px);
    sum += sampleCanvas(uv + vec2( 1.0,  1.0) * px);
    sum += sampleCanvas(uv + vec2( 0.0, -1.5) * px) * 0.7;
    sum += sampleCanvas(uv + vec2( 0.0,  1.5) * px) * 0.7;
    sum += sampleCanvas(uv + vec2(-1.5,  0.0) * px) * 0.7;
    sum += sampleCanvas(uv + vec2( 1.5,  0.0) * px) * 0.7;
    sum /= 6.8;

    // Threshold: only bright stuff bleeds.
    float lum = dot(sum, vec3(0.299, 0.587, 0.114));
    float t = smoothstep(0.45, 0.95, lum);
    return sum * t;
}

void main() {
    vec2 uv = v_texCoord;

    // Subtle barrel distortion.
    vec2 centered = uv - 0.5;
    float r2 = dot(centered, centered);
    vec2 warped = uv + centered * r2 * 0.04;

    // Chromatic aberration grows toward edges.
    float aberr = 0.0015 * (0.4 + r2 * 4.0);
    float r = sampleCanvas(warped + vec2(aberr, 0.0)).r;
    float g = sampleCanvas(warped).g;
    float b = sampleCanvas(warped - vec2(aberr, 0.0)).b;
    vec3 color = vec3(r, g, b);

    // Add bloom on top.
    vec3 glow = bloom(warped);
    color += glow * (0.55 + 0.4 * u_flash);

    // Faint scanlines (very subtle).
    float scan = 0.97 + 0.03 * sin(warped.y * u_resolution.y * 3.14159);
    color *= scan;

    // Soft vignette.
    float vignette = smoothstep(0.95, 0.25, r2 * 2.2);
    color *= mix(0.85, 1.0, vignette);

    // Flash boost on line clear: brief brighten + slight cool tint shift.
    color += vec3(0.12, 0.18, 0.25) * u_flash;

    // Slow breathing brightness (whoa-but-subtle).
    float breathe = 0.985 + 0.015 * sin(u_time * 1.2);
    color *= breathe;

    // Mask: black outside the warped sampling region.
    float mask = step(0.0, warped.x) * step(warped.x, 1.0)
               * step(0.0, warped.y) * step(warped.y, 1.0);

    fragColor = vec4(color * mask, 1.0);
}
