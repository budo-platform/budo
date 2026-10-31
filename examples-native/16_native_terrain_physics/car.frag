#version 300 es
precision highp float;
in vec3 v_normal;
in vec3 v_local;
in vec3 v_world_position;
uniform vec3 u_eye;
uniform vec3 u_fog_color;
uniform vec3 u_light_direction;
uniform vec3 u_color;
uniform float u_fog_start;
uniform float u_fog_end;
out vec4 out_color;
void main() {
    vec3 normal = normalize(v_normal);
    vec3 light = normalize(u_light_direction);
    vec3 view_direction = normalize(u_eye - v_world_position);
    vec3 half_direction = normalize(light + view_direction);
    float diffuse = max(dot(normal, light), 0.0);
    float specular = pow(max(dot(normal, half_direction), 0.0), 28.0) * 0.35;
    float bottom = clamp(-v_local.z * 4.0, 0.0, 1.0);
    vec3 base = mix(u_color, u_color * 0.45, bottom);
    vec3 lit = base * (0.22 + diffuse * 0.9) + vec3(1.0, 0.88, 0.65) * specular;
    float fog = smoothstep(u_fog_start, u_fog_end, distance(v_world_position, u_eye));
    out_color = vec4(mix(lit, u_fog_color, fog), 1.0);
}
