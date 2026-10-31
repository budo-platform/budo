#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;
uniform vec2 u_mouse;
uniform float u_radius;
uniform float u_twist;

in vec2 v_texCoord;

void main() {
    vec2 uv = v_texCoord;
    vec2 center = u_mouse / max(u_resolution, vec2(1.0));

    vec2 delta = uv - center;
    float dist = length(delta);
    float normalized = dist / u_radius;

    if (normalized < 1.0) {
        // Swirl: rotate pixels based on distance from center
        float falloff = 1.0 - normalized * normalized;
        float angle = falloff * u_twist + u_time * 1.5;
        float s = sin(angle);
        float c = cos(angle);
        delta = vec2(c * delta.x - s * delta.y, s * delta.x + c * delta.y);
    }

    vec2 swirled = center + delta;

    vec4 color = texture(u_canvas, swirled);

    // Subtle radial color shift near vortex center
    float glow = exp(-dist * dist * 20.0);
    vec3 tint = vec3(0.4, 0.2, 0.8) * glow * 0.5;

    fragColor = vec4(color.rgb + tint, 1.0);
}
