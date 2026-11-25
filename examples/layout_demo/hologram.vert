#version 300 es
precision highp float;
in vec3 a_position;
in vec3 a_normal;
in vec4 a_color;
uniform vec3 u_eye;
uniform vec3 u_right;
uniform vec3 u_up;
uniform vec3 u_forward;
uniform vec2 u_lens;
uniform vec2 u_shift;
out vec4 v_color;
out vec3 v_normal;
out vec3 v_position;

void main() {
    vec3 p = a_position - u_eye;
    float depth = dot(p, u_forward);
    gl_Position = vec4(u_lens.x * dot(p, u_right) / u_lens.y, u_lens.x * dot(p, u_up),
                       depth - 1.0, depth);
    gl_Position.xy += u_shift * depth;
    v_color = a_color;
    v_normal = a_normal;
    v_position = a_position;
}
