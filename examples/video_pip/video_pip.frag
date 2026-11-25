#version 300 es
precision highp float;

uniform sampler2D u_video;

in vec2 v_texCoord;
out vec4 fragColor;

void main() {
    fragColor = texture(u_video, v_texCoord);
}
