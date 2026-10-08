#version 300 es
precision highp float;
in vec3 a_position;
in vec2 a_uv;     // x: across the track (-1..1), y: distance along it
in vec3 a_color;  // birth time (s), opacity, unused

uniform mat4 u_mvp;
uniform float u_now;

out vec2 v_track;
out float v_opacity;
out vec3 v_world_pos;

void main() {
    // Tracks fade out over FADE seconds; the sand slowly fills them in.
    const float FADE = 24.0;
    float age = u_now - a_color.x;
    v_opacity = a_color.y * clamp(1.0 - age / FADE, 0.0, 1.0);
    v_track = a_uv;
    v_world_pos = a_position;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
