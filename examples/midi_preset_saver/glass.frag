#version 300 es
precision highp float;
out vec4 fragColor;

uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;

in vec2 v_texCoord;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main() {
    vec2 uv = v_texCoord;
    vec2 px = 1.0 / max(u_resolution, vec2(1.0));
    vec2 centered = uv - vec2(0.5);

    float waveA = sin((uv.y * 11.0) + (u_time * 0.35));
    float waveB = sin((uv.x * 17.0) - (u_time * 0.22));
    vec2 warp = vec2(waveA * 0.65, waveB * 0.45) * px * 0.95;

    vec4 base = texture(u_canvas, uv + warp);
    vec3 leftSample = texture(u_canvas, uv + warp - vec2(px.x * 1.2, 0.0)).rgb;
    vec3 rightSample = texture(u_canvas, uv + warp + vec2(px.x * 1.2, 0.0)).rgb;

    vec3 color = base.rgb;
    color.r = mix(color.r, rightSample.r, 0.08);
    color.b = mix(color.b, leftSample.b, 0.08);

    float diagonal = smoothstep(0.018, 0.0, abs((uv.x + uv.y) - 1.12));
    float softBand = smoothstep(0.05, 0.0, abs((uv.x * 0.7 + uv.y) - 0.42));
    float edge = smoothstep(0.74, 0.18, length(centered));
    float grain = hash(floor(uv * u_resolution * 0.38) + floor(u_time * 8.0)) - 0.5;

    vec3 sheen = vec3(1.0, 0.78, 0.52) * diagonal * 0.026;
    sheen += vec3(0.58, 0.72, 0.66) * softBand * 0.012;
    color = color * (0.992 + edge * 0.014) + sheen;
    color += grain * 0.008;

    float vignette = smoothstep(0.92, 0.25, length(centered));
    color *= 0.965 + vignette * 0.035;

    fragColor = vec4(color, base.a);
}
