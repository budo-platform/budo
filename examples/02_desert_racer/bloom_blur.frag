#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_source;
uniform vec2 u_step;  // texel * direction * spread
in vec2 v_texCoord;

void main() {
    // 9-tap Gaussian in 5 bilinear samples.
    vec3 c = texture(u_source, v_texCoord).rgb * 0.2270270;
    c += texture(u_source, v_texCoord + u_step * 1.3846154).rgb * 0.3162162;
    c += texture(u_source, v_texCoord - u_step * 1.3846154).rgb * 0.3162162;
    c += texture(u_source, v_texCoord + u_step * 3.2307692).rgb * 0.0702703;
    c += texture(u_source, v_texCoord - u_step * 3.2307692).rgb * 0.0702703;
    fragColor = vec4(c, 1.0);
}
