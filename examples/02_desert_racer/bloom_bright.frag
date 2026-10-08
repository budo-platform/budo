#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_scene;
uniform vec2 u_texel;       // one scene pixel, in UV
uniform float u_threshold;  // brightness where bloom starts
uniform float u_scene_flip; // 1: the scene target is stored bottom-up
in vec2 v_texCoord;

void main() {
    // Read the scene upright, so the bloom targets are stored like any pass output.
    vec2 uv = vec2(v_texCoord.x, mix(v_texCoord.y, 1.0 - v_texCoord.y, u_scene_flip));
    // Four bilinear taps average a 4x4 block while downsampling.
    vec3 c = texture(u_scene, uv + u_texel * vec2(-1.0, -1.0)).rgb;
    c += texture(u_scene, uv + u_texel * vec2(1.0, -1.0)).rgb;
    c += texture(u_scene, uv + u_texel * vec2(-1.0, 1.0)).rgb;
    c += texture(u_scene, uv + u_texel * vec2(1.0, 1.0)).rgb;
    c *= 0.25;
    float peak = max(c.r, max(c.g, c.b));
    // Soft knee, so bloom fades in instead of switching on.
    float knee = clamp(peak - u_threshold + 0.12, 0.0, 0.24);
    knee = knee * knee / 0.96;
    float weight = max(knee, peak - u_threshold) / max(peak, 1e-4);
    fragColor = vec4(c * weight, 1.0);
}
