#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;
uniform float u_segments;

in vec2 v_texCoord;

void main() {
    vec2 uv = v_texCoord;
    vec2 centered = uv - 0.5;

    // Convert to polar coordinates
    float angle = atan(centered.y, centered.x);
    float radius = length(centered);

    // Kaleidoscope: mirror the angle into repeated segments
    float seg = 3.14159 * 2.0 / u_segments;
    angle = mod(angle, seg);
    // Mirror alternate segments
    if (angle > seg * 0.5) {
        angle = seg - angle;
    }

    // Add time-based rotation
    angle += u_time * 0.3;

    // Convert back to cartesian
    vec2 kaleidoUV = vec2(cos(angle), sin(angle)) * radius + 0.5;

    vec4 color = texture(u_canvas, kaleidoUV);

    // Boost saturation slightly
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = mix(vec3(gray), color.rgb, 1.4);

    // Radial fade
    float fade = 1.0 - smoothstep(0.35, 0.52, radius);
    color.rgb *= mix(0.3, 1.0, fade);

    fragColor = vec4(color.rgb, 1.0);
}
