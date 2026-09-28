#version 300 es
precision highp float;
out vec4 fragColor;
in vec3 v_normal;
in vec3 v_color;
in vec3 v_world_pos;

uniform vec3 u_eye;
uniform vec3 u_fog_color;
uniform vec3 u_light_dir;
uniform vec3 u_light_color;
uniform float u_ambient;
uniform float u_fog_start;
uniform float u_fog_end;

void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(u_light_dir);
    vec3 v = normalize(u_eye - v_world_pos);
    float diffuse = max(dot(n, l), 0.0);
    float backLight = pow(max(dot(-n, l), 0.0), 2.0) * 0.20;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 2.0) * 0.08;
    vec3 lit = v_color * (vec3(u_ambient + backLight + rim) + diffuse * 0.84 * u_light_color);

    float dist = distance(v_world_pos, u_eye);
    float fog = smoothstep(u_fog_start, u_fog_end, dist);
    lit = mix(lit, u_fog_color, fog);

    fragColor = vec4(lit, 1.0);
}
