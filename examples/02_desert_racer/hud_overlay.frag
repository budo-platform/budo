#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec3 u_sky_color; // chroma-key color (the sky fill used by Skia)

in vec2 v_texCoord;

void main() {
    vec4 src = texture(u_canvas, v_texCoord);
    // Distance from sky color in RGB space. Anti-aliased HUD edges blend
    // between the HUD color and the sky, so they are far from the sky color
    // and will pass through opaque.
    float d = distance(src.rgb, u_sky_color);
    float keep = smoothstep(0.02, 0.05, d);
    fragColor = vec4(src.rgb, src.a * keep);
}
