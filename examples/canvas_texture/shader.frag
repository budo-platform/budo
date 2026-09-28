#version 300 es
precision highp float;

uniform sampler2D u_ui;
uniform float u_time;
uniform float u_time_scale;

in vec2 v_texCoord;
out vec4 fragColor;

void main() {
    vec2 uv = v_texCoord;
    vec2 panelUv = uv;
    float inside = step(0.0, panelUv.x) * step(panelUv.x, 1.0) * step(0.0, panelUv.y) * step(panelUv.y, 1.0);
    vec2 shimmerUv = panelUv + vec2(sin(u_time + u_time_scale + panelUv.y * 8.0) * 0.01, 0.0);
    vec4 ui = texture(u_ui, shimmerUv);

    vec3 background = mix(vec3(0.04, 0.07, 0.12), vec3(0.10, 0.16, 0.24), uv.y);
    vec3 color = mix(background, ui.rgb, ui.a * inside);
    fragColor = vec4(color, 1.0);

    /*vec2 uv = v_texCoord;
    vec2 panelUv = vec2((uv.x - 0.18) / 0.64, (uv.y - 0.32) / 0.36);
    float inside = step(0.0, panelUv.x) * step(panelUv.x, 1.0) * step(0.0, panelUv.y) * step(panelUv.y, 1.0);
    vec2 shimmerUv = panelUv + vec2(sin(u_time + u_time_scale + panelUv.y * 8.0) * 0.01, 0.0);
    vec4 ui = texture(u_ui, shimmerUv);
    vec3 background = mix(vec3(0.04, 0.07, 0.12), vec3(0.10, 0.16, 0.24), uv.y);
    vec3 color = mix(background, ui.rgb, ui.a * inside);
    fragColor = vec4(color, 1.0);*/
}
