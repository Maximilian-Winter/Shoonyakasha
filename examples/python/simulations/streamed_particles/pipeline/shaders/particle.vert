#version 450
// One point per particle, its position from the stream, its colour a blend
// of the attractors' colours weighted by closeness, brighter when faster.

layout(std140, set = 0, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
    uint attractorCount;
    float screenHeight;
    float pointSize;
} camera;

layout(std430, set = 0, binding = 1) readonly buffer Particles { vec4 positionSpeed[]; } particles;

struct Attractor { vec4 positionStrength; vec4 color; };
layout(std430, set = 0, binding = 2) readonly buffer Attractors { Attractor items[]; } attractors;

layout(location = 0) out vec3 vColor;

void main() {
    vec4 p = particles.positionSpeed[gl_VertexIndex];
    vec4 clip = camera.proj * camera.view * vec4(p.xyz, 1.0);
    gl_Position = clip;
    // pointSize is in world units: about its height in pixels at this distance.
    gl_PointSize = clamp(camera.pointSize * camera.proj[1][1] * camera.screenHeight * 0.5 / max(clip.w, 1e-3),
                         1.0, 24.0);

    vec3 color = vec3(0.0);
    float total = 0.0;
    for (uint i = 0u; i < min(camera.attractorCount, 16u); ++i) {
        vec3 d = attractors.items[i].positionStrength.xyz - p.xyz;
        float w = 1.0 / (dot(d, d) + 0.05);
        color += attractors.items[i].color.rgb * w;
        total += w;
    }
    color = total > 0.0 ? color / total : vec3(0.6, 0.7, 1.0);
    vColor = color * (0.3 + 0.08 * clamp(p.w, 0.0, 10.0));
}
