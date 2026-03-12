#version 450

//
// GPU Particle Vertex Shader — reads from SSBO, no vertex input
// 點即是星  星即是粒
// A point is a star, a star is a particle
//

struct Particle {
    vec4 position;   // xyz = position, w = life
    vec4 velocity;   // xyz = velocity, w = mass
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(binding = 1) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) out float outLife;
layout(location = 1) out float outSpeed;

void main() {
    Particle p = particles[gl_VertexIndex];

    gl_Position = camera.proj * camera.view * vec4(p.position.xyz, 1.0);
    gl_PointSize = max(1.0, 4.0 * p.position.w / max(gl_Position.w, 0.01));

    outLife = p.position.w;
    outSpeed = length(p.velocity.xyz);
}
