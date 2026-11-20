#version 300 es
precision highp float;

in vec2 v_texCoord;
uniform sampler2D u_background;
uniform vec2 u_resolution;
uniform bool u_idlePattern;
out vec4 fragColor;

void main() {
    fragColor = u_idlePattern && gl_FragCoord.y >= u_resolution.y * 0.5
        ? vec4(0, 0, 1, 1) : texture(u_background, v_texCoord);
}
