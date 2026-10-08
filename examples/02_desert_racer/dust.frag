#version 300 es
precision highp float;
out vec4 fragColor;
in vec2 v_corner;
in float v_alpha;
in float v_seed;
in vec3 v_world_pos;

uniform vec3 u_eye;
uniform vec3 u_dust_color;
uniform vec3 u_light_dir;
uniform vec3 u_light_color;
uniform float u_ambient;
uniform vec3 u_sun_dir;
uniform vec3 u_sun_color;
uniform vec3 u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;
uniform vec3 u_head_pos;      // the player's headlights
uniform vec3 u_head_dir;
uniform float u_head_on;
uniform vec2 u_near_fade;     // puffs fade in between these camera distances

void main() {
    float r = length(v_corner);
    if (r > 1.0) discard;
    // Soft round puff with a few lumps, no texture.
    float lumps = 0.78 + 0.22 * sin(v_corner.x * 5.0 + v_seed * 40.0) * sin(v_corner.y * 4.0 - v_seed * 23.0);
    float density = smoothstep(1.0, 0.15, r) * lumps;

    vec3 view = normalize(v_world_pos - u_eye);
    // Dust glows when backlit by a low sun.
    float forward = pow(max(dot(view, u_sun_dir), 0.0), 5.0);
    vec3 lit = u_dust_color * (vec3(u_ambient) + u_light_color * (0.45 + 0.35 * u_light_dir.z));
    lit += u_sun_color * forward * 0.9;

    // Caught in the player's headlight beams at night.
    vec3 toPuff = v_world_pos - u_head_pos;
    float dist = length(toPuff);
    float cone = smoothstep(0.82, 0.96, dot(toPuff / max(dist, 1e-3), u_head_dir));
    lit += vec3(1.0, 0.85, 0.55) * cone * u_head_on * 1.6 / (1.0 + dist * dist * 0.004);

    float eyeDist = distance(v_world_pos, u_eye);
    float fog = smoothstep(u_fog_start, u_fog_end, eyeDist);
    lit = mix(lit, u_fog_color, fog);
    // Fade puffs close to the camera: the chase camera rides inside the trail.
    float nearFade = smoothstep(u_near_fade.x, u_near_fade.y, eyeDist);
    fragColor = vec4(lit, density * v_alpha * nearFade);
}
