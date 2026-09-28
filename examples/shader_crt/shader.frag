#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;
uniform float u_scanline_intensity;
uniform float u_aberration;

in vec2 v_texCoord;

void main() {
    vec2 uv = v_texCoord;

    // Barrel distortion (CRT curvature)
    vec2 centered = uv - 0.5;
    float r2 = dot(centered, centered);
    vec2 warped = uv + centered * r2 * 0.15;

    // Chromatic aberration: offset R and B channels
    float offset = u_aberration * 0.004;
    float r = texture(u_canvas, warped + vec2(offset, 0.0)).r;
    float g = texture(u_canvas, warped).g;
    float b = texture(u_canvas, warped - vec2(offset, 0.0)).b;
    vec3 color = vec3(r, g, b);

    // Scanlines
    float scanline = sin(warped.y * u_resolution.y * 3.14159) * 0.5 + 0.5;
    scanline = mix(1.0, scanline, u_scanline_intensity);
    color *= scanline;

    // Flicker
    float flicker = 0.97 + 0.03 * sin(u_time * 8.0);
    color *= flicker;

    // Vignette
    float vignette = 1.0 - r2 * 2.8;
    vignette = clamp(vignette, 0.0, 1.0);
    color *= vignette;

    // Slight green tint for old-school feel
    color *= vec3(0.95, 1.0, 0.92);

    // Black outside the warped area
    float mask = step(0.0, warped.x) * step(warped.x, 1.0) * step(0.0, warped.y) * step(warped.y, 1.0);

    fragColor = vec4(color * mask, 1.0);
}
