#version 300 es
precision highp float;
in vec2 a_position;
in vec2 a_uv;
uniform float u_angle;
uniform float u_aspect;
out vec2 v_uv;
void main() {
  float c = cos(u_angle);
  float s = sin(u_angle);
  vec2 p = mat2(c, -s, s, c) * a_position;
  p.x /= u_aspect;
  gl_Position = vec4(p, 0.0, 1.0);
  v_uv = a_uv;
}
