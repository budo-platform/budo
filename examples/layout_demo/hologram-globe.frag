#version 300 es
precision highp float;
uniform vec3 u_eye;
uniform float u_rotation;
uniform vec4 u_wipe; // classic globe: discard inside the hologram wipe circle (x, y top-down px, radius, height)
in vec3 v_normal;
in vec3 v_position;
out vec4 fragColor;

float landmass(vec2 p, vec2 center, vec2 extent, float slant) {
    vec2 d = p - center;
    d.x = mod(d.x + 3.141593, 6.283186) - 3.141593;
    d.x += d.y * slant;
    return 1.0 - length(d / extent);
}

void main() {
    if (u_wipe.z > 0.0 && length(vec2(gl_FragCoord.x, u_wipe.w - gl_FragCoord.y) - u_wipe.xy) < u_wipe.z) discard;
    vec3 n = normalize(v_normal);
    vec3 view = normalize(u_eye - v_position);
    vec2 map = vec2(atan(n.x, n.z) + u_rotation, asin(clamp(n.y, -1.0, 1.0)));
    float land = landmass(map, vec2(-1.48, 0.64), vec2(0.54, 0.40), 0.61);
    land = max(land, landmass(map, vec2(-1.15, 0.37), vec2(0.35, 0.29), 0.58));
    land = max(land, landmass(map, vec2(-0.88, -0.30), vec2(0.34, 0.50), 0.36));
    land = max(land, landmass(map, vec2(-0.67, 1.13), vec2(0.23, 0.29), -0.44));
    land = max(land, landmass(map, vec2(0.24, 0.08), vec2(0.37, 0.51), 0.27));
    land = max(land, landmass(map, vec2(0.46, 0.75), vec2(0.52, 0.30), -0.48));
    land = max(land, landmass(map, vec2(1.61, 0.72), vec2(0.97, 0.42), 0.78));
    land = max(land, landmass(map, vec2(1.59, 0.35), vec2(0.40, 0.35), 0.39));
    land = max(land, landmass(map, vec2(2.40, -0.44), vec2(0.35, 0.24), -0.52));
    land += 0.12 * sin(map.x * 23.0 + map.y * 11.0) * sin(map.y * 26.0)
          + 0.061 * sin(map.x * 67.0) * cos(map.y * 53.0);

    vec2 cells = abs(fract(map * vec2(46.0, 50.0)) - 0.5);
    float dots = 1.0 - smoothstep(0.15, 0.37, length(cells));
    vec2 grid = abs(fract(map * vec2(3.82, 3.82)) - 0.5);
    float graticule = smoothstep(0.471, 0.499, max(grid.x, grid.y));
    float rim = pow(1.0 - max(dot(n, view), 0.0), 3.4);
    float scan = 0.62 + 0.38 * sin(map.y * 230.0);
    vec3 color = vec3(0.012, 0.069, 0.19);
    color += vec3(0.04, 0.33, 0.71) * rim * 1.3;
    color += vec3(0.02, 0.23, 0.49) * graticule * 0.34;
    color += vec3(0.025, 0.38, 0.73) * smoothstep(-0.018, 0.04, land) * (0.25 + 0.68 * dots) * scan;
    color += vec3(0.10, 0.65, 1.0) * (1.0 - smoothstep(0.0, 0.065, abs(land))) * 0.49;
    fragColor = vec4(color, 1.0);
}
