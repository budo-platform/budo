#version 300 es
precision highp float;
in vec3 a_position;
in vec3 a_normal;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform float u_height_scale; // peak amplitude used to normalize coloring

out vec3 v_normal;
out vec3 v_color;
out vec3 v_world_pos;

void main() {
    // Desert palette: warm sand in low rolling areas, ochre dunes at mid
    // height, and rockier muted ridges on steep / high ground.
    float h = (a_position.z + u_height_scale) / (u_height_scale * 2.0);
    h = clamp(h, 0.0, 1.0);

    vec3 dryWash = vec3(0.47, 0.29, 0.13);
    vec3 redSand = vec3(0.72, 0.43, 0.19);
    vec3 duneSand = vec3(0.93, 0.70, 0.39);
    vec3 paleRidge = vec3(0.86, 0.76, 0.55);
    vec3 desertRock = vec3(0.50, 0.37, 0.26);

    vec3 col = mix(dryWash, redSand, smoothstep(0.00, 0.32, h));
    col = mix(col, duneSand, smoothstep(0.24, 0.62, h));
    col = mix(col, paleRidge, smoothstep(0.62, 1.00, h));

    float slope = 1.0 - clamp(a_normal.z, 0.0, 1.0);
    col = mix(col, desertRock, smoothstep(0.18, 0.62, slope) * 0.55);

    // Subtle sandstone banding so the infinite terrain is less flat-colored.
    float strata = 0.5 + 0.5 * sin(a_position.z * 3.8 + a_position.x * 0.17 + a_position.y * 0.11);
    col *= mix(0.92, 1.08, strata);

    v_color  = col;
    v_normal = mat3(u_model) * a_normal;
    v_world_pos = (u_model * vec4(a_position, 1.0)).xyz;

    gl_Position = u_mvp * vec4(a_position, 1.0);
}
