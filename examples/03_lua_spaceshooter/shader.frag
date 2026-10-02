#version 300 es
precision highp float;
out vec4 fragColor;
in vec2 v_texCoord;

uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;
uniform float u_bloom_intensity;
uniform float u_hit_flash;   // 0..1
uniform float u_warp;        // 0..1 screen-warp on explosion

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Fast 13-tap blur (separated into one pass for simplicity)
vec3 blur13(sampler2D tex, vec2 uv, vec2 texel, float radius) {
    vec3 col = vec3(0.0);
    float w[5];
    w[0] = 0.204164;
    w[1] = 0.180174;
    w[2] = 0.123832;
    w[3] = 0.066282;
    w[4] = 0.027631;
    col += texture(tex, uv).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        vec2 off = texel * float(i) * radius;
        col += (texture(tex, uv + off).rgb + texture(tex, uv - off).rgb) * w[i];
    }
    // diagonal taps for a bit more spread
    for (int i = 1; i < 4; i++) {
        vec2 off = texel * vec2(float(i), float(i)) * radius * 0.7071;
        col += (texture(tex, uv + off).rgb + texture(tex, uv - off).rgb) * w[i] * 0.5;
        off = texel * vec2(float(i), -float(i)) * radius * 0.7071;
        col += (texture(tex, uv + off).rgb + texture(tex, uv - off).rgb) * w[i] * 0.5;
    }
    return col;
}

void main() {
    vec2 uv = v_texCoord;
    vec2 texel = 1.0 / u_resolution;

    // ---- WARP / SHOCKWAVE on hit ----
    if (u_warp > 0.0) {
        vec2 centered = uv - 0.5;
        float r = length(centered);
        float ring = smoothstep(0.0, 0.3, r) * smoothstep(0.5, 0.3, r);
        float warpAmt = ring * u_warp * 0.035;
        uv += normalize(centered) * warpAmt;
    }

    // ---- CRT barrel distortion ----
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    uv = uv + c * r2 * 0.04;

    // ---- Sample canvas with chromatic aberration ----
    float aberr = 0.003 + r2 * 0.008;
    vec2 dir = normalize(c + vec2(0.0001));
    float rC = texture(u_canvas, uv + dir * aberr).r;
    float gC = texture(u_canvas, uv).g;
    float bC = texture(u_canvas, uv - dir * aberr).b;
    vec3 original = vec3(rC, gC, bC);
    float lum = dot(original, vec3(0.299, 0.587, 0.114));

    // ---- Procedural starfield (only where canvas is black) ----
    vec3 col = original;
    if (lum < 0.04) {
        vec2 p = (uv - 0.5) * 2.0;
        p.x *= u_resolution.x / u_resolution.y;

        // Layer 1: dense small stars
        float stars = 0.0;
        for (float i = 0.0; i < 4.0; i += 1.0) {
            float speed = 0.22 + i * 0.12;
            vec2 grid = fract(p * (8.0 + i * 5.0) + vec2(0.0, u_time * speed)) - 0.5;
            float h = hash(floor(p * (8.0 + i * 5.0) + vec2(0.0, u_time * speed)) + vec2(i * 73.0));
            if (h > 0.92) {
                float d = length(grid);
                float twinkle = 0.75 + 0.25 * sin(u_time * (3.0 + h * 7.0) + h * 43.0);
                stars += smoothstep(0.10, 0.0, d) * twinkle * (1.0 - i * 0.15);
            }
        }

        // Layer 2: sparse bright stars with cross-flare
        for (float i = 0.0; i < 2.0; i += 1.0) {
            vec2 grid2 = fract(p * 2.5 + vec2(i * 37.0, u_time * 0.08)) - 0.5;
            float h2 = hash(floor(p * 2.5 + vec2(i * 37.0, u_time * 0.08)) + vec2(i * 11.0));
            if (h2 > 0.97) {
                float d2 = length(grid2);
                float flare = smoothstep(0.08, 0.0, d2);
                // cross-spike
                float spike = max(smoothstep(0.04, 0.0, abs(grid2.x)) * smoothstep(0.2, 0.0, abs(grid2.y)),
                                  smoothstep(0.04, 0.0, abs(grid2.y)) * smoothstep(0.2, 0.0, abs(grid2.x)));
                stars += (flare + spike * 0.5) * 1.8;
            }
        }

        // Nebula: multi-layer swirling
        float neb1 = sin(p.x * 1.5 + u_time * 0.07) * sin(p.y * 1.5 + u_time * 0.05);
        float neb2 = sin(p.x * 3.0 - p.y * 2.0 + u_time * 0.12) * 0.5;
        float neb3 = sin(length(p) * 4.0 - u_time * 0.2) * 0.3;
        float nebula = (neb1 + neb2 + neb3) * 0.33 + 0.33;

        vec3 nebulaColor = mix(vec3(0.04, 0.0, 0.12), vec3(0.12, 0.02, 0.25), nebula);
        nebulaColor += vec3(0.0, 0.0, 0.06) * sin(p.x * 5.0 + u_time * 0.3);

        col = nebulaColor + vec3(stars * 0.85, stars * 0.9, stars);
    }

    // ---- Bloom: wide Gaussian on bright areas ----
    vec3 blurred = blur13(u_canvas, uv, texel, 3.5);
    float brightness = dot(blurred, vec3(0.299, 0.587, 0.114));
    vec3 glow = blurred * smoothstep(0.25, 0.9, brightness);
    // Second wider pass for halo
    vec3 halo = blur13(u_canvas, uv, texel, 9.0);
    float haloBrightness = dot(halo, vec3(0.299, 0.587, 0.114));
    vec3 haloGlow = halo * smoothstep(0.15, 0.7, haloBrightness);

    col += glow * u_bloom_intensity + haloGlow * (u_bloom_intensity * 0.4);

    // Neon color boost on sprites
    if (lum > 0.04) {
        col *= vec3(1.05, 1.15, 1.3); // cool neon blue-shift
    }

    // ---- Hit flash ----
    col = mix(col, vec3(1.0, 0.2, 0.1), u_hit_flash * 0.5);

    // ---- Scanlines ----
    float scanline = sin(uv.y * u_resolution.y * 3.14159) * 0.5 + 0.5;
    col *= mix(1.0, scanline, 0.12);

    // ---- Rolling light streak ----
    float streak = smoothstep(0.02, 0.0, mod(uv.y + u_time * 0.26, 0.18)) * 0.04;
    col += vec3(streak);

    // ---- Vignette ----
    float vig = 1.0 - r2 * 2.2;
    col *= clamp(vig, 0.0, 1.0);

    // ---- Tone mapping (Reinhard) ----
    col = col / (col + vec3(1.0));
    col = pow(col, vec3(0.9)); // slightly brighter gamma

    // Black outside barrel
    float mask = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    fragColor = vec4(col * mask, 1.0);
}
