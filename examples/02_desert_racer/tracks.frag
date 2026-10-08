#version 300 es
precision highp float;
out vec4 fragColor;
in vec2 v_track;
in float v_opacity;
in vec3 v_world_pos;

uniform vec3 u_eye;
uniform vec3 u_track_color;
uniform float u_fog_start;
uniform float u_fog_end;

void main() {
    // Soft edges and a tread pattern of ridges across the track.
    float edge = 1.0 - smoothstep(0.55, 1.0, abs(v_track.x));
    float tread = 0.72 + 0.28 * step(0.5, fract(v_track.y * 2.6 + abs(v_track.x) * 0.8));
    float fog = smoothstep(u_fog_start * 0.6, u_fog_end * 0.8, distance(v_world_pos, u_eye));
    fragColor = vec4(u_track_color, edge * tread * v_opacity * (1.0 - fog));
}
