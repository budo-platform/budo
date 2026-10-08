#version 300 es
precision highp float;
out vec4 fragColor;
in vec3 v_dir;

// World is Z-up. Colors come from the day/night cycle in main.js.
uniform vec3 u_zenith;
uniform vec3 u_horizon;     // equals the fog color, so terrain melts into the sky
uniform vec3 u_sky_glow;    // sunset glow toward the sun's azimuth
uniform vec3 u_sun_dir;     // true sun direction, below the horizon at night
uniform vec3 u_sun_color;   // zero once the sun has set
uniform vec3 u_moon_dir;
uniform float u_night;      // stars and moon visibility
uniform float u_star_angle; // the star field turns with the cycle
uniform float u_cloud_time;
uniform int u_cloud_octaves; // fewer on phones: the clouds cover most pixels
uniform float u_time;

float hash3(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash2(i);
    float b = hash2(i + vec2(1.0, 0.0));
    float c = hash2(i + vec2(0.0, 1.0));
    float d = hash2(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float cloudFbm(vec2 p) {
    float total = 0.0;
    float amplitude = 0.5;
    float range = 0.0;
    for (int i = 0; i < 4; i++) {
        if (i >= u_cloud_octaves) break;
        total += valueNoise(p) * amplitude;
        range += amplitude;
        p = p * 2.03 + vec2(1.7, 9.2);
        amplitude *= 0.5;
    }
    // The same 0..0.9375 range as four octaves, so cover stays the same.
    return total / range * 0.9375;
}

void main() {
    vec3 d = normalize(v_dir);
    float h = d.z;
    float up = max(h, 0.0);

    // Base gradient: the haze band stays thin, the zenith takes most of the sky.
    vec3 col = mix(u_horizon, u_zenith, pow(smoothstep(0.0, 0.85, up), 0.6));

    // Sunset glow, hugging the horizon on the sun's side.
    vec2 sunXY = normalize(u_sun_dir.xy + vec2(1e-5));
    vec2 dirXY = normalize(d.xy + vec2(1e-5));
    float toward = dot(dirXY, sunXY) * 0.5 + 0.5;
    float band = exp(-up * 5.0);
    col += u_sky_glow * pow(toward, 3.0) * band;

    // Sun: wide Mie halo, tight glare, then the disc.
    float mu = max(dot(d, u_sun_dir), 0.0);
    float aboveHorizon = smoothstep(-0.012, 0.01, h);
    col += u_sun_color * (pow(mu, 10.0) * 0.22 + pow(mu, 220.0) * 0.55);
    col += u_sun_color * smoothstep(0.99935, 0.99965, mu) * 2.5 * aboveHorizon;

    // Stars, on a sphere that turns slowly. Each 3D cell may hold one star.
    if (u_night > 0.01) {
        float c = cos(u_star_angle), s = sin(u_star_angle);
        vec3 sd = vec3(d.x, c * d.y - s * d.z, s * d.y + c * d.z);
        vec3 p = sd * 95.0;
        vec3 cell = floor(p);
        float pick = hash3(cell);
        if (pick > 0.955) {
            vec3 center = vec3(hash3(cell + 3.1), hash3(cell + 7.7), hash3(cell + 1.9)) * 0.6 + 0.2;
            float dist = length(fract(p) - center);
            float twinkle = 0.65 + 0.35 * sin(u_time * (2.0 + pick * 30.0) + pick * 400.0);
            float brightness = (pick - 0.955) / 0.045;
            float star = smoothstep(0.26, 0.0, dist) * mix(0.3, 1.2, brightness * brightness) * twinkle;
            vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.75), hash3(cell + 5.3));
            col += tint * star * u_night * smoothstep(0.02, 0.22, h);
        }
    }

    // Moon with a soft cold halo.
    float moonMu = max(dot(d, u_moon_dir), 0.0);
    vec3 moonColor = vec3(0.92, 0.94, 1.0);
    col += moonColor * (pow(moonMu, 40.0) * 0.10) * u_night;
    col = mix(col, moonColor, smoothstep(0.99955, 0.9998, moonMu) * u_night * aboveHorizon);

    // Thin high clouds, projected on a plane above the camera.
    if (h > 0.0) {
        vec2 uv = d.xy / (h + 0.12) * 1.6 + vec2(u_cloud_time, u_cloud_time * 0.35);
        float shape = cloudFbm(uv * vec2(1.0, 2.6));
        float cover = smoothstep(0.50, 0.78, shape) * smoothstep(0.0, 0.18, h);
        vec3 lit = mix(u_zenith * 1.6 + vec3(0.08), vec3(1.0, 0.98, 0.94), 1.0 - u_night);
        lit += u_sky_glow * 1.4 * pow(toward, 2.0) + u_sun_color * pow(mu, 8.0) * 0.6;
        col = mix(col, lit, cover * 0.55);
    }

    fragColor = vec4(min(col, vec3(1.0)), 1.0);
}
