#version 450
//
// Lambert lighting from the sun. Surfaces with a gridStrength material
// parameter get longitude and latitude lines from their texture coordinates
// (degrees) at five spacings, 10 down to 0.001 degrees; each spacing fades in
// once its lines are far enough apart on screen.
//

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float gridStrength;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragLongLat;

layout(location = 0) out vec4 outColor;

// Coverage of lines at every whole value of `coord`, anti-aliased over one
// pixel, faded out where the lines come closer than a few pixels.
float gridLines(vec2 coord) {
    vec2 width = fwidth(coord);
    vec2 distanceToLine = abs(fract(coord - 0.5) - 0.5) / max(width, vec2(1e-9));
    vec2 line = 1.0 - clamp(distanceToLine - 0.5, 0.0, 1.0);
    vec2 fade = 1.0 - smoothstep(0.08, 0.25, width);
    return max(line.x * fade.x, line.y * fade.y);
}

void main() {
    vec3 normal = normalize(fragNormal);

    float lines = 0.0;
    float spacing = 10.0;
    for (int i = 0; i < 5; ++i) {
        lines = max(lines, gridLines(fragLongLat / spacing) * (1.0 - 0.15 * float(i)));
        spacing *= 0.1;
    }
    lines *= push.gridStrength;

    vec3 albedo = push.baseColorFactor.rgb * fragColor;
    albedo = mix(albedo, vec3(0.95, 0.85, 0.45), 0.6 * lines);

    float diffuse = max(dot(normal, camera.sunDirection.xyz), 0.0);
    vec3 lit = albedo * (0.03 + 0.97 * diffuse);

    // The swapchain is sRGB: writing to it encodes the linear colour.
    outColor = vec4(lit, 1.0);
}
