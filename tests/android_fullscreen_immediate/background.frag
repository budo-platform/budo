#version 300 es
precision highp float;

uniform vec2 u_resolution;
uniform vec2 u_expectedResolution;
uniform sampler2D u_canvas;
uniform float u_expectedBlue;
out vec4 fragColor;

void main() {
    vec3 canvas = texture(u_canvas, vec2(0.5)).rgb;
    bool correct = all(equal(u_resolution, u_expectedResolution))
        && canvas.g > 0.9 && abs(canvas.b - u_expectedBlue) < 0.1;
    fragColor = correct ? vec4(1, 0, 0, 1) : vec4(1, 0, 1, 1);
}
