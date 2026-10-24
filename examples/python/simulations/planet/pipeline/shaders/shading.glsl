// Shading shared by the terrain and the blocks: Lambert lighting from the
// sun, and lines from the cube face's (u, v) in [-1, 1] at every
// power-of-two subdivision of the face, from its edges (level 0) to 2^16
// squares across; each level fades in once its lines are far enough apart
// on screen.

// Coverage of lines at every whole value of `coord`, anti-aliased over one
// pixel, faded out where the lines come closer than a few pixels.
float gridLines(vec2 coord) {
    vec2 width = fwidth(coord);
    vec2 distanceToLine = abs(fract(coord - 0.5) - 0.5) / max(width, vec2(1e-9));
    vec2 line = 1.0 - clamp(distanceToLine - 0.5, 0.0, 1.0);
    vec2 fade = 1.0 - smoothstep(0.08, 0.25, width);
    return max(line.x * fade.x, line.y * fade.y);
}

// The lit colour of a surface with `albedo`, `gridStrength` (0 none, 1 full)
// times the face lines at `faceUv`, facing `normal`.
vec3 shade(vec3 albedo, float gridStrength, vec2 faceUv, vec3 normal, vec3 sunDirection) {
    vec2 faceUnit = faceUv * 0.5 + 0.5;   // 0 .. 1 across the face
    float lines = 0.0;
    for (int level = 0; level <= 16; ++level) {
        lines = max(lines, gridLines(faceUnit * exp2(float(level))) * (1.0 - 0.035 * float(level)));
    }
    albedo = mix(albedo, vec3(0.95, 0.85, 0.45), 0.6 * lines * gridStrength);
    float diffuse = max(dot(normalize(normal), sunDirection), 0.0);
    return albedo * (0.03 + 0.97 * diffuse);
}
