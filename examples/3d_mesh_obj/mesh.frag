#version 300 es
precision highp float;
out vec4 fragColor;
in vec3 v_normal;

void main() {
    vec3 n = normalize(v_normal);
    vec3 light_dir = normalize(vec3(0.4, 0.8, 0.5));
    float diffuse = max(dot(n, light_dir), 0.0);
    float ambient = 0.2;
    vec3 base = vec3(1.0, 0.75, 0.45);
    vec3 lit = base * (ambient + diffuse * 0.9);
    fragColor = vec4(lit, 1.0);
}
