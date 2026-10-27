#version 450
// One point per nucleon, sized to cover its sphere on screen; points past
// the count are moved off screen.

struct Nucleon {
    vec3 position;
    float type;
    vec3 velocity;
    float energy;
    vec3 force;
    uint nucleusId;
};

layout(std430, set = 0, binding = 0) readonly buffer Nucleons { Nucleon n[]; } nucleons;
layout(std140, set = 0, binding = 1) uniform View {
    mat4 view;
    mat4 proj;
    vec2 screen;          // width, height
    uint count;
    float radius;         // sphere radius, world units
    float glowEnergy;     // kinetic energy at which a nucleon glows fully
} view;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vGlow;
layout(location = 2) out vec3 vCenterView;

void main() {
    if (uint(gl_VertexIndex) >= view.count) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        return;
    }
    Nucleon nucleon = nucleons.n[gl_VertexIndex];
    vec4 center = view.view * vec4(nucleon.position, 1.0);
    gl_Position = view.proj * center;
    gl_PointSize = clamp(2.0 * view.radius * abs(view.proj[1][1]) * 0.5 * view.screen.y / max(-center.z, 1e-3), 1.0, 256.0);
    vCenterView = center.xyz;
    vColor = nucleon.type > 0.5 ? vec3(1.0, 0.36, 0.16) : vec3(0.42, 0.58, 0.85);
    vGlow = clamp(nucleon.energy / view.glowEnergy, 0.0, 1.0);
}
