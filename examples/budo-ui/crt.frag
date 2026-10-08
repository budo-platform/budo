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
    vec2 centered = uv - 0.5;
    float radius = dot(centered, centered);
    vec2 warped = uv + centered * radius * 0.15;

    float offset = u_aberration * 0.004;
    vec3 color = vec3(
        texture(u_canvas, warped + vec2(offset, 0.0)).r,
        texture(u_canvas, warped).g,
        texture(u_canvas, warped - vec2(offset, 0.0)).b
    );
    float scanline = sin(warped.y * u_resolution.y * 3.14159) * 0.5 + 0.5;
    color *= mix(1.0, scanline, u_scanline_intensity);
    color *= 0.97 + 0.03 * sin(u_time * 8.0);
    color *= clamp(1.0 - radius * 2.8, 0.0, 1.0);
    color *= vec3(0.95, 1.0, 0.92);

    float mask = step(0.0, warped.x) * step(warped.x, 1.0) *
        step(0.0, warped.y) * step(warped.y, 1.0);
    fragColor = vec4(color * mask, 1.0);
}