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
uniform vec3 u_sky_glow;
uniform vec3 u_sun_dir;
uniform vec3 u_sun_color;

// The sky's horizon color in this direction (see sky.frag), so distant
// geometry fades into the sunset glow instead of a flat fog band.
vec3 fogColorToward(vec3 dir) {
    vec2 sunXY = normalize(u_sun_dir.xy + vec2(1e-5));
    vec2 dirXY = normalize(dir.xy + vec2(1e-5));
    float toward = dot(dirXY, sunXY) * 0.5 + 0.5;
    float mu = max(dot(dir, u_sun_dir), 0.0);
    return u_fog_color + u_sky_glow * pow(toward, 3.0) + u_sun_color * pow(mu, 10.0) * 0.22;
}

void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(u_light_dir);
    vec3 v = normalize(u_eye - v_world_pos);

    // Cheap modern terrain shading: wrap light keeps dunes readable at grazing
    // angles, slope AO adds grounded contrast, and a tiny horizon rim gives
    // sunlit ridges definition. No new textures/passes/draw calls.
    float wrapDiffuse = clamp((dot(n, l) + 0.30) / 1.30, 0.0, 1.0);
    float slopeAO = mix(0.74, 1.0, smoothstep(0.16, 0.92, n.z));
    float ridgeRim = pow(1.0 - max(dot(n, v), 0.0), 2.0) * smoothstep(0.10, 0.74, n.z) * 0.10;
    vec3 lit = v_color * (vec3(u_ambient * slopeAO) + wrapDiffuse * 0.74 * u_light_color);
    lit += u_light_color * ridgeRim;

    float dist = distance(v_world_pos, u_eye);
    float fog = smoothstep(u_fog_start, u_fog_end, dist);
    lit = mix(lit, fogColorToward(normalize(v_world_pos - u_eye)), fog);

    fragColor = vec4(lit, 1.0);
}
