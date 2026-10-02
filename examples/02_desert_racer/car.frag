#version 300 es
precision highp float;
out vec4 fragColor;
uniform vec3 u_color;
uniform vec3 u_light_dir;
uniform vec3 u_light_color;
uniform vec3 u_eye;
uniform vec3 u_fog_color;
uniform float u_ambient;
uniform float u_headlight_on;
uniform float u_fog_start;
uniform float u_fog_end;

in vec3 v_normal;
in vec3 v_local;
in vec3 v_world_pos;

void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(u_light_dir);
    vec3 v = normalize(u_eye - v_world_pos);
    vec3 h = normalize(l + v);
    float diffuse = max(dot(n, l), 0.0);
    // Slight darkening on the bottom face for visual depth.
    float bottom = clamp(-v_local.z * 4.0, 0.0, 1.0);
    vec3 base = mix(u_color, u_color * 0.5, bottom);
    vec3 lit = base * (vec3(u_ambient) + diffuse * 0.85 * u_light_color);
    // Low-cost clearcoat/specular and fresnel edge highlight for more modern
    // vehicle readability. Cars cover few pixels, so this is essentially free.
    float spec = pow(max(dot(n, h), 0.0), 28.0) * (1.0 - bottom) * 0.36;
    float fresnel = pow(1.0 - max(dot(n, v), 0.0), 3.0) * (1.0 - bottom) * 0.12;
    lit += u_light_color * spec + vec3(0.70, 0.82, 1.0) * fresnel;
    float front = smoothstep(0.43, 0.50, v_local.y);
    float lensX = 1.0 - smoothstep(0.04, 0.13, abs(abs(v_local.x) - 0.24));
    float lensZ = 1.0 - smoothstep(0.04, 0.16, abs(v_local.z - 0.08));
    float headlight = front * lensX * lensZ * u_headlight_on;
    lit += vec3(1.0, 0.86, 0.48) * headlight * 2.4;

    float dist = distance(v_world_pos, u_eye);
    float fog = smoothstep(u_fog_start, u_fog_end, dist);
    lit = mix(lit, u_fog_color, fog);

    fragColor = vec4(lit, 1.0);
}
