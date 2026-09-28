#version 300 es
precision highp float;
out vec4 fragColor;
uniform sampler2D u_canvas;
uniform vec2 u_resolution;
uniform float u_time;

in vec2 v_texCoord;

// Rotation matrix around Y axis
mat3 rotY(float a) {
    float c = cos(a), s = sin(a);
    return mat3(c, 0.0, s,
                0.0, 1.0, 0.0,
                -s, 0.0, c);
}

// Rotation matrix around X axis
mat3 rotX(float a) {
    float c = cos(a), s = sin(a);
    return mat3(1.0, 0.0, 0.0,
                0.0, c, -s,
                0.0, s, c);
}

// SDF for a box
float sdBox(vec3 p, vec3 b) {
    vec3 d = abs(p) - b;
    return min(max(d.x, max(d.y, d.z)), 0.0) + length(max(d, 0.0));
}

// Scene distance function
float scene(vec3 p) {
    mat3 rot = rotY(u_time * 0.7) * rotX(u_time * 0.5);
    vec3 rp = rot * p;
    return sdBox(rp, vec3(0.8));
}

// Compute normal via gradient
vec3 getNormal(vec3 p) {
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        scene(p + e.xyy) - scene(p - e.xyy),
        scene(p + e.yxy) - scene(p - e.yxy),
        scene(p + e.yyx) - scene(p - e.yyx)
    ));
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution) / min(u_resolution.x, u_resolution.y);

    // Camera setup
    vec3 ro = vec3(0.0, 0.0, 3.5);
    vec3 rd = normalize(vec3(uv, -1.5));

    // Raymarch
    float t = 0.0;
    float d;
    for (int i = 0; i < 80; i++) {
        vec3 p = ro + rd * t;
        d = scene(p);
        if (d < 0.001) break;
        t += d;
        if (t > 20.0) break;
    }

    vec4 base = texture(u_canvas, v_texCoord);

    if (d < 0.001) {
        vec3 p = ro + rd * t;
        vec3 n = getNormal(p);

        // Rotate normal for face coloring
        mat3 rot = rotY(u_time * 0.7) * rotX(u_time * 0.5);
        vec3 rn = rot * n;

        // Light direction
        vec3 lightDir = normalize(vec3(0.8, 1.0, 0.6));
        float diff = max(dot(n, lightDir), 0.0);
        float amb = 0.15;

        // Color each face differently
        vec3 faceColor;
        vec3 an = abs(rn);
        if (an.x > an.y && an.x > an.z) {
            faceColor = rn.x > 0.0 ? vec3(0.9, 0.2, 0.2) : vec3(0.2, 0.9, 0.4);
        } else if (an.y > an.z) {
            faceColor = rn.y > 0.0 ? vec3(0.2, 0.4, 0.9) : vec3(0.9, 0.9, 0.2);
        } else {
            faceColor = rn.z > 0.0 ? vec3(0.9, 0.5, 0.1) : vec3(0.6, 0.2, 0.9);
        }

        // Specular highlight
        vec3 viewDir = normalize(-rd);
        vec3 reflDir = reflect(-lightDir, n);
        float spec = pow(max(dot(viewDir, reflDir), 0.0), 32.0);

        // Edge darkening (wireframe-like effect on edges)
        float edge = smoothstep(0.0, 0.04, d);

        vec3 color = faceColor * (amb + diff * 0.85) + vec3(1.0) * spec * 0.5;

        // Slight fog based on distance
        float fog = exp(-0.08 * t * t);
        vec3 bgColor = base.rgb;
        color = mix(bgColor, color, fog);

        fragColor = vec4(color, 1.0);
    } else {
        // Background: keep the canvas content
        fragColor = base;
    }
}
