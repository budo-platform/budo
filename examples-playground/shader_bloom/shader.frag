#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;
uniform float u_bloom_radius;
uniform float u_bloom_intensity;

in vec2 v_texCoord;

void main() {
    vec2 uv = v_texCoord;
    vec2 texel = 1.0 / u_resolution;

    vec4 original = texture(u_canvas, uv);

    // 9-tap Gaussian blur for bloom
    float radius = u_bloom_radius;
    vec4 bloom = vec4(0.0);
    float total = 0.0;

    for (float x = -2.0; x <= 2.0; x += 1.0) {
        for (float y = -2.0; y <= 2.0; y += 1.0) {
            vec2 off = vec2(x, y) * texel * radius;
            float w = exp(-(x * x + y * y) * 0.2);
            bloom += texture(u_canvas, uv + off) * w;
            total += w;
        }
    }
    bloom /= total;

    // Extract bright areas (threshold)
    float brightness = dot(bloom.rgb, vec3(0.299, 0.587, 0.114));
    vec3 glow = bloom.rgb * smoothstep(0.35, 0.8, brightness);

    // Combine original + bloom
    vec3 result = original.rgb + glow * u_bloom_intensity;

    // Slight tone mapping
    result = result / (result + vec3(1.0));

    fragColor = vec4(result, 1.0);
}
