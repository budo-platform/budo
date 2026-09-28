#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_albedo;

in vec2 v_uv;
in vec3 v_normal;

void main() {
    vec3 n = normalize(v_normal);
    vec3 light_dir = normalize(vec3(0.4, 0.8, 0.5));
    float diffuse = max(dot(n, light_dir), 0.0);
    float ambient = 0.3;
    vec4 albedo = texture(u_albedo, v_uv);
    vec3 lit = albedo.rgb * (ambient + diffuse * 0.85);
    fragColor = vec4(lit, albedo.a);
}
