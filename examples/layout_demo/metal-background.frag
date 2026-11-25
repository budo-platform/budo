#version 300 es
precision highp float;
uniform vec3 u_eye;
uniform vec3 u_right;
uniform vec3 u_up;
uniform vec3 u_forward;
uniform vec2 u_lens;
uniform vec4 u_shock; // xz center, age in seconds, strength
in vec2 v_uv;
out vec4 fragColor;

vec2 hash2(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz) * q.zy);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i).x, hash2(i + vec2(1, 0)).x, f.x),
               mix(hash2(i + vec2(0, 1)).x, hash2(i + vec2(1, 1)).x, f.x), f.y);
}
float strata(vec2 p) {
    return 0.52 * noise(p) + 0.29 * noise(p * 2.12) + 0.14 * noise(p * 4.31)
         + 0.05 * noise(p * 12.2);
}
float crack(vec2 p) {
    vec2 cell = floor(p), f = fract(p);
    vec2 distances = vec2(9.0);
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 g = vec2(float(i), float(j));
            vec2 d = g + 0.17 + 0.66 * hash2(cell + g) - f;
            float s = dot(d, d);
            distances = s < distances.x ? vec2(s, distances.x) : vec2(distances.x, min(s, distances.y));
        }
    }
    return distances.y - distances.x;
}
float band(float d, float width) {
    return exp(-d * d / (width * width));
}
float rock(vec3 p) {
    vec3 q = abs(p + vec3(0.0, 0.46, 0.32)) - vec3(6.54, 0.30, 4.28);
    float chips = noise(p.xz * 4.0 + p.y * 5.0) * 0.10 + noise(p.xz * 11.0 - p.y * 7.0) * 0.052;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - 0.22 + chips;
}
float imprint(vec2 p) {
    float r = length(p);
    float rings = 1.0 - smoothstep(0.010, 0.034, min(abs(r - 0.55), abs(r - 0.45)));
    float a = atan(p.y, p.x + 0.00001);
    float spokes = (1.0 - smoothstep(0.03, 0.17, abs(sin(a * 6.0)))) *
                   smoothstep(0.14, 0.32, r) * (1.0 - smoothstep(0.34, 0.40, r));
    return max(rings, spokes);
}

void main() {
    // Region UVs are top-down; the camera basis uses positive screen-up.
    vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0);
    vec3 ray = normalize(u_forward + (u_right * ndc.x * u_lens.y + u_up * ndc.y) / u_lens.x);
    float groundT = (-1.10 - u_eye.y) / ray.y;
    vec2 ground = (u_eye + ray * groundT).xz;
    float backdrop = strata(ground * 1.9);
    float shadow = smoothstep(0.0, 1.0, length(max(abs(ground + vec2(0.0, 0.32)) - vec2(6.54, 4.28), 0.0)));
    vec3 color = mix(vec3(0.018, 0.020, 0.026), vec3(0.085, 0.066, 0.055), backdrop) *
                 mix(0.24, 1.0, shadow);
    float front = u_shock.z * 6.5;
    float fade = u_shock.w * exp(-u_shock.z * 1.15);
    float reach = length(ground - u_shock.xy);
    float wave = fade * (band(reach - front, 0.45) + 0.5 * band(reach - front * 0.6, 0.25));
    color += vec3(0.05, 0.25, 0.6) * wave * 0.5;

    float t = (0.32 - u_eye.y) / ray.y;
    float d = 0.0;
    vec3 p = u_eye + ray * t;
    for (int i = 0; i < 32; ++i) {
        p = u_eye + ray * t;
        d = rock(p);
        if (d < 0.007 || t >= groundT) break;
        t += max(d * 0.73, 0.009);
    }
    if (d < 0.007 && t < groundT) {
        vec2 e = vec2(0.020, 0.0);
        vec3 normal = normalize(vec3(rock(p + e.xyy) - rock(p - e.xyy),
                                     rock(p + e.yxy) - rock(p - e.yxy),
                                     rock(p + e.yyx) - rock(p - e.yyx)));
        float hit = length(p.xz - u_shock.xy);
        float shock = fade * (band(hit - front, 0.4) + 0.5 * band(hit - front * 0.6, 0.22));
        vec2 st = p.xz + p.y * vec2(1.2, 1.6) + normalize(p.xz - u_shock.xy + 0.0001) * shock * 0.06;
        float coarse = strata(st * 0.87);
        float grit = strata(st * 43.0);
        vec2 warp = vec2(noise(st * 7.0), noise(st * 7.0 + 29.0)) * 0.24;
        float fissure = smoothstep(0.025, 0.13, crack(st * vec2(1.4, 1.9) + warp));
        float flakes = smoothstep(0.025, 0.15, crack(st * 6.9 + warp));
        float oxide = smoothstep(0.38, 0.76, coarse * 0.73 + grit * 0.27);
        float stamp = max(imprint(st + vec2(5.55, -0.75)),
                          max(imprint(st - vec2(5.55, -1.26)), imprint(st - vec2(0.14, 3.10))));
        float amber = max(dot(normal, normalize(vec3(0.82, 0.37, 0.42))), 0.0);
        vec3 base = mix(vec3(0.14, 0.16, 0.21), vec3(0.38, 0.23, 0.15), oxide);
        color = base * (0.33 + 0.88 * amber) * (0.28 + grit * 1.35)
                * mix(0.25, 1.0, fissure) * mix(0.56, 1.0, flakes);
        color += vec3(0.063, 0.032, 0.014) * stamp;
        color *= 1.0 - 0.62 * smoothstep(0.0, 0.88, -p.y);
        float blue = 0.46 * exp(-dot(p.xz, p.xz) * 0.23)
                   + 0.21 * exp(-dot(p.xz - vec2(3.2, -2.7), p.xz - vec2(3.2, -2.7)) * 0.25)
                   + 0.25 * exp(-dot(p.xz + vec2(3.9, 2.8), p.xz + vec2(3.9, 2.8)) * 0.35)
                   + 0.17 * exp(-dot(p.xz - vec2(3.4, 2.6), p.xz - vec2(3.4, 2.6)) * 0.35);
        color += vec3(0.033, 0.22, 0.57) * blue * fissure * (0.32 + grit);
        color += vec3(0.10, 0.55, 1.0) * shock * (0.25 + 2.2 * (1.0 - fissure) + 0.6 * stamp);
    }
    color *= max(0.48, 1.0 - 0.35 * dot(ndc, ndc));
    fragColor = vec4(color, 1.0);
}
