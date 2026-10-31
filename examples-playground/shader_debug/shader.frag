#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;

in vec2 v_texCoord;

void main() {
    vec4 color = texture(u_canvas, v_texCoord);
    // Apply a strong red tint so it's obvious the shader is working
    fragColor = vec4(color.r * 1.0, color.g * 0.3, color.b * 0.3, color.a);
}
