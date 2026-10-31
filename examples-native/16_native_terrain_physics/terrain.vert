#version 300 es
precision highp float;
in vec3 a_position;
in vec3 a_normal;
uniform mat4 u_mvp;
out vec3 v_normal;
out vec3 v_color;
out vec3 v_world_position;
void main() {
    float h = clamp((a_position.z + 3.2) / 6.4, 0.0, 1.0);
    vec3 dry_wash = vec3(0.47, 0.29, 0.13);
    vec3 red_sand = vec3(0.72, 0.43, 0.19);
    vec3 dune_sand = vec3(0.93, 0.70, 0.39);
    vec3 ridge = vec3(0.86, 0.76, 0.55);
    vec3 rock = vec3(0.50, 0.37, 0.26);
    vec3 color = mix(dry_wash, red_sand, smoothstep(0.0, 0.32, h));
    color = mix(color, dune_sand, smoothstep(0.24, 0.62, h));
    color = mix(color, ridge, smoothstep(0.62, 1.0, h));
    color = mix(color, rock, smoothstep(0.18, 0.62, 1.0 - a_normal.z) * 0.55);
    color *= mix(0.92, 1.08, 0.5 + 0.5 * sin(a_position.z * 3.8 + a_position.x * 0.17));
    v_color = color;
    v_normal = a_normal;
    v_world_position = a_position;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
