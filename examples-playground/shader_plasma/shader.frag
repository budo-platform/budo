#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;
uniform vec2 u_center;
uniform float u_scale;
uniform float u_zoomPhase;
uniform float u_mobile;

in vec2 v_texCoord;

const float TAU = 6.28318530718;
const float LOG3 = 1.09861228867;

mat2 rot(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c);
}

float tri(float x) {
    return abs(fract(x) - 0.5);
}

vec3 palette(float t) {
    return 0.50 + 0.50 * cos(TAU * (vec3(0.08, 0.31, 0.61) + t));
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec2 hash22(vec2 p) {
    float x = hash21(p);
    float y = hash21(p + x + 19.19);
    return vec2(x, y);
}

vec3 sampleTravelField(vec2 p, float travel, vec2 drift) {
    vec2 q = p;
    float density = 0.0;
    float filaments = 0.0;
    float haze = 0.0;
    float weight = 1.0;

    for (int i = 0; i < 8; i++) {
        if (u_mobile > 0.5 && i >= 5) {
            break;
        }

        float layer = float(i);
        float depth = travel + layer * 0.73;
        float layerRot = depth * 0.55 + layer * 0.41 + drift.x * 0.9;
        vec2 warped = rot(layerRot) * q;
        warped += drift * (0.16 + 0.035 * layer);

        vec2 groveGrid = warped * (0.72 + 0.08 * sin(depth * 0.35 + layer));
        vec2 groveCell = floor(groveGrid);
        vec2 groveJitter = hash22(groveCell + vec2(layer, floor(depth))) - 0.5;
        vec2 groveCenter = groveCell + 0.5 + groveJitter * 0.65;
        vec2 groveLocal = groveGrid - groveCenter;

        float radius = length(groveLocal) + 0.0001;
        float angle = atan(groveLocal.y, groveLocal.x);
        float scaleBeat = pow(3.0, fract(depth));
        float shellCoord = log(radius) / LOG3 - depth;
        float shell = tri(shellCoord);
        float trunkSway = 0.28 * sin(depth * 1.2 + groveLocal.y * 1.4 + drift.x * 2.1 + hash21(groveCell) * 2.0);
        float trunk = exp(-30.0 * abs(groveLocal.x - trunkSway) * (0.75 + 0.35 * shell));
        float branchFreq = 4.0 + mod(layer, 3.0);
        float branchCurve = angle + 0.55 * sin(radius * branchFreq * 1.6 - depth * 1.3);
        float branchSpread = 0.18 + 0.07 * sin(depth * 1.8 + layer * 0.7);
        float branchMask = exp(-34.0 * abs(tri(branchCurve / TAU * branchFreq + shell * 0.65) - branchSpread));
        float twigMask = exp(-44.0 * abs(tri((groveLocal.x + groveLocal.y * 1.7) * scaleBeat * 0.42 - depth * 0.37) - (0.16 + 0.05 * cos(depth + layer))));
        float canopyShape = exp(-18.0 * abs(tri(log(radius + 1.0) * 1.8 + angle * 0.9 - depth * 0.41) - (0.20 + 0.05 * sin(depth * 1.1))));
        float rootMask = exp(-26.0 * abs(tri((abs(groveLocal.x) + max(groveLocal.y, -0.2)) * scaleBeat * 0.30 + depth * 0.26) - (0.23 + 0.04 * sin(drift.y * 3.1 + depth))));
        float shellMask = exp(-22.0 * shell);

        density += (trunk * 0.95 + branchMask * 0.75 + canopyShape * 0.62 + shellMask * 0.24) * weight;
        filaments += (branchMask + twigMask * 0.86 + rootMask * 0.72 + trunk * 0.25) * weight;
        haze += (canopyShape * 0.80 + shellMask * 0.40 + twigMask * 0.32) * weight;

        q = rot(0.22 + layer * 0.09) * q * 1.16 + drift * 0.07;
        weight *= 0.61;
    }

    return vec3(density, filaments, haze);
}

void main() {
    vec2 uv = v_texCoord;
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 centered = vec2((uv.x - 0.5) * 2.0 * aspect, (0.5 - uv.y) * 2.0);
    vec2 p = u_center + centered * u_scale;

    float travel = -u_zoomPhase + u_time * 0.035;
    vec2 centerField = vec2(
        sin(u_center.x * 0.85 + u_center.y * 0.55 + u_time * 0.06),
        cos(u_center.y * 0.95 - u_center.x * 0.45 - u_time * 0.05)
    );
    vec2 flowDir = normalize(vec2(cos(travel * 0.8 + centerField.x), sin(travel * 0.65 + centerField.y)) + centerField * 0.35);
    vec2 drift = flowDir * (0.32 + 0.10 * sin(travel * 1.4)) + centerField * 0.22;

    vec3 fieldNear = sampleTravelField(p, travel, drift);
    vec3 fieldAhead = sampleTravelField(p + flowDir * u_scale * 0.42, travel + 0.38, drift * 1.12 + centerField * 0.08);
    vec3 fieldFar = vec3(0.0);

    if (u_mobile > 0.5) {
        fieldFar = fieldNear * 0.35 + fieldAhead * 0.20;
    } else {
        fieldFar = sampleTravelField(p - flowDir * u_scale * 0.86, travel - 0.46, drift * 0.88 - centerField * 0.06);
    }

    float density = fieldNear.x * 0.58 + fieldAhead.x * 0.27 + fieldFar.x * 0.15;
    float filaments = fieldNear.y * 0.52 + fieldAhead.y * 0.33 + fieldFar.y * 0.15;
    float haze = fieldNear.z * 0.50 + fieldAhead.z * 0.24 + fieldFar.z * 0.26;

    float radial = exp(-0.20 * length(centered));
    float surge = 0.5 + 0.5 * sin(density * 1.9 - filaments * 1.1 + travel * 2.4);
    float shimmer = 0.5 + 0.5 * cos(haze * 2.3 + density * 0.7 - travel * 1.8);
    vec3 col = palette(density * 0.11 + filaments * 0.05 + travel * 0.08);
    col *= 0.45 + 0.95 * radial;
    col += surge * vec3(0.06, 0.11, 0.18);
    col += shimmer * vec3(0.14, 0.08, 0.06);
    col += filaments * vec3(0.10, 0.14, 0.18) * 0.38;
    col += haze * vec3(0.08, 0.05, 0.12) * 0.28;

    vec4 base = texture(u_canvas, uv);
    float blend = clamp(0.26 + density * 0.26 + filaments * 0.12 + haze * 0.10, 0.26, 0.90);
    fragColor = vec4(mix(col, base.rgb, blend), 1.0);
}
