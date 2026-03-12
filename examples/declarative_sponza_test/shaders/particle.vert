#version 450
//
// Sponza Particle Vertex Shader
//
// Each point is a spark of wisdom — transformed from world space to screen
// Reads particle data from SSBO via gl_VertexIndex (no vertex buffers)
//

struct Particle {
    vec4 position;   // xyz = position, w = life
    vec4 velocity;   // xyz = velocity, w = mass
};

// set 1 = particleRenderSet (SSBO)
layout(std430, set = 1, binding = 0) readonly buffer ParticleBuffer {
    Particle particles[];
};

// set 0 = cameraSet (reuses existing dot-path CameraUBO)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;    // xyz = camera world position
    vec4 params;      // x = near, y = far, z = fov, w = aspect
} camera;

layout(location = 0) out float outLife;
layout(location = 1) out float outSpeed;

void main() {
    Particle p = particles[gl_VertexIndex];

    // Transform to clip space
    vec4 viewPos = camera.view * vec4(p.position.xyz, 1.0);
    gl_Position = camera.proj * viewPos;

    // Point size: larger when alive and close, smaller when far or dying
    // Sponza atrium is large (~28m wide) so scale accordingly
    float lifeFactor = clamp(p.position.w * 0.5, 0.0, 1.0);
    float depthFactor = 1.0 / max(abs(viewPos.z), 0.1);
    gl_PointSize = clamp(30.0 * lifeFactor * depthFactor, 2.0, 24.0);

    // Pass to fragment shader
    outLife = p.position.w;
    outSpeed = length(p.velocity.xyz);
}
