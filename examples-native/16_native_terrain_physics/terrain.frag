#version 300 es
precision highp float;
in vec3 v_normal;
in vec3 v_color;
in vec3 v_world_position;
uniform vec3 u_eye;
uniform vec3 u_fog_color;
uniform vec3 u_light_direction;
uniform float u_fog_start;
uniform float u_fog_end;
out vec4 out_color;
void main() {
    vec3 normal = normalize(v_normal);
    float diffuse = clamp((dot(normal, normalize(u_light_direction)) + 0.3) / 1.3, 0.0, 1.0);
    float slope_ao = mix(0.74, 1.0, smoothstep(0.16, 0.92, normal.z));
    vec3 lit = v_color * (0.30 * slope_ao + diffuse * 0.78);
    float fog = smoothstep(u_fog_start, u_fog_end, distance(v_world_position, u_eye));
    out_color = vec4(mix(lit, u_fog_color, fog), 1.0);
}
