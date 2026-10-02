#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;

// Custom uniforms
uniform float u_glow_intensity;
uniform float u_fog_density;
uniform float u_vignette_strength;
uniform float u_saturation;
uniform float u_contrast;

in vec2 v_texCoord;

// Color grading - warm tones for outdoors
vec3 colorGrade(vec3 color) {
    // Warm highlights, cool shadows
    vec3 shadows = vec3(0.1, 0.15, 0.25);
    vec3 highlights = vec3(1.1, 1.0, 0.9);
    
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    vec3 graded = mix(color * shadows * 2.0, color * highlights, luminance);
    
    return graded;
}

// Saturation adjustment
vec3 adjustSaturation(vec3 color, float saturation) {
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    return mix(vec3(luminance), color, saturation);
}

// Contrast adjustment
vec3 adjustContrast(vec3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

// Vignette effect
float vignette(vec2 uv, float strength) {
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    return 1.0 - smoothstep(0.3, 0.9, dist * strength);
}

void main() {
    vec2 uv = v_texCoord;
    vec2 texel = 1.0 / u_resolution;
    
    // Sample the original canvas
    vec4 original = texture(u_canvas, uv);
    vec3 color = original.rgb;
    
    // === GLOW / BLOOM PASS ===
    // Multi-pass blur for bloom effect on bright areas (force arrows)
    vec3 bloom = vec3(0.0);
    float bloomTotal = 0.0;
    float bloomRadius = 4.0;
    
    for (float x = -3.0; x <= 3.0; x += 1.0) {
        for (float y = -3.0; y <= 3.0; y += 1.0) {
            vec2 offset = vec2(x, y) * texel * bloomRadius;
            float weight = exp(-(x * x + y * y) * 0.15);
            vec3 sample = texture(u_canvas, uv + offset).rgb;
            
            // Only bloom bright colors (the force arrows are bright)
            float brightness = max(max(sample.r, sample.g), sample.b);
            if (brightness > 0.5) {
                bloom += sample * weight * brightness;
                bloomTotal += weight;
            }
        }
    }
    
    if (bloomTotal > 0.0) {
        bloom /= bloomTotal;
        color += bloom * u_glow_intensity;
    }
    
    // === ATMOSPHERIC FOG ===
    // Subtle blue-ish fog to add depth
    vec3 fogColor = vec3(0.3, 0.4, 0.6);
    float fogAmount = u_fog_density * 0.15;
    
    // Sample brightness for pseudo-depth (brighter = closer)
    float brightness = dot(original.rgb, vec3(0.299, 0.587, 0.114));
    float depthFog = (1.0 - brightness) * fogAmount;
    color = mix(color, fogColor, depthFog * 0.3);
    
    // === COLOR GRADING ===
    color = colorGrade(color);
    color = adjustSaturation(color, u_saturation);
    color = adjustContrast(color, u_contrast);
    
    // === CHROMATIC ABERRATION (subtle) ===
    float aberrationAmount = 0.002;
    vec2 direction = normalize(uv - 0.5);
    float dist = length(uv - 0.5);
    
    vec3 aberrated;
    aberrated.r = texture(u_canvas, uv + direction * dist * aberrationAmount).r;
    aberrated.g = original.g;
    aberrated.b = texture(u_canvas, uv - direction * dist * aberrationAmount).b;
    
    color = mix(color, colorGrade(adjustSaturation(aberrated, u_saturation)), 0.3);
    
    // === VIGNETTE ===
    float vig = vignette(uv, u_vignette_strength);
    color *= vig;
    
    // === SUBTLE SCANLINES (optional retro feel) ===
    float scanline = sin(uv.y * u_resolution.y * 1.5) * 0.03 + 0.97;
    color *= scanline;
    
    // === FILM GRAIN ===
    float grain = fract(sin(dot(uv + u_time * 0.01, vec2(12.9898, 78.233))) * 43758.5453);
    color += (grain - 0.5) * 0.03;
    
    // === TONE MAPPING ===
    // Reinhard tone mapping for HDR-like effect
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    fragColor = vec4(color, 1.0);
}
