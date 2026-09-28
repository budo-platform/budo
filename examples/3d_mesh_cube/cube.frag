#version 300 es
precision highp float;
out vec4 fragColor;
in vec3 v_normal;
in vec3 v_color;

void main() {
    vec3 n = normalize(v_normal);
    vec3 light_dir = normalize(vec3(0.4, 0.8, 0.5));
    float diffuse = max(dot(n, light_dir), 0.0);
    float ambient = 0.25;
    vec3 lit = v_color * (ambient + diffuse * 0.85);
    fragColor = vec4(lit, 1.0);
}
