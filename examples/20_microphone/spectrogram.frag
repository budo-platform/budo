#version 300 es
precision highp float;

// The spectrogram: one texture column per analysis, written in a ring.
// Row 0 is the lowest frequency; values are levels from -90 dB (0) to 0 dB (1).
uniform sampler2D u_spectrum;
uniform float u_newest;  // the column written last
uniform float u_columns;
uniform float u_rows;

in vec2 v_texCoord;
out vec4 fragColor;

// Dark blue to magenta to orange to pale yellow.
vec3 palette(float t) {
    vec3 c0 = vec3(0.059, 0.078, 0.133);
    vec3 c1 = vec3(0.235, 0.141, 0.471);
    vec3 c2 = vec3(0.776, 0.212, 0.443);
    vec3 c3 = vec3(0.984, 0.553, 0.239);
    vec3 c4 = vec3(0.988, 0.980, 0.749);
    t = clamp(t, 0.0, 1.0) * 4.0;
    if (t < 1.0) return mix(c0, c1, t);
    if (t < 2.0) return mix(c1, c2, t - 1.0);
    if (t < 3.0) return mix(c2, c3, t - 2.0);
    return mix(c3, c4, t - 3.0);
}

void main() {
    // The newest column on the right, the oldest on the left.
    int columns = int(u_columns);
    int age = columns - 1 - min(int(v_texCoord.x * u_columns), columns - 1);
    int column = (int(u_newest) - age + columns) % columns;
    // High frequencies at the top; blend between rows.
    float row = (1.0 - v_texCoord.y) * u_rows - 0.5;
    int r0 = clamp(int(floor(row)), 0, int(u_rows) - 1);
    int r1 = min(r0 + 1, int(u_rows) - 1);
    float a = texelFetch(u_spectrum, ivec2(column, r0), 0).r;
    float b = texelFetch(u_spectrum, ivec2(column, r1), 0).r;
    fragColor = vec4(palette(mix(a, b, clamp(fract(row), 0.0, 1.0))), 1.0);
}
