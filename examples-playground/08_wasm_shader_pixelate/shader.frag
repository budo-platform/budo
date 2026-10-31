#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;
uniform float u_pixel_size;

in vec2 v_texCoord;

void main() {
    vec2 uv = v_texCoord;

    // Pixelate: snap UV to grid
    float px = u_pixel_size / u_resolution.x;
    float py = u_pixel_size / u_resolution.y;
    vec2 snapped = vec2(floor(uv.x / px + 0.5) * px, floor(uv.y / py + 0.5) * py);

    vec4 color = texture(u_canvas, snapped);

    // Add subtle grid lines between pixels
    vec2 gridPos = fract(uv / vec2(px, py));
    float border = step(0.92, max(gridPos.x, gridPos.y));
    color.rgb *= 1.0 - border * 0.2;

    fragColor = color;
}
