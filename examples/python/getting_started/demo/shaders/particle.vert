#version 450
//
// Particle Vertex Shader — Enlarged, with kaleidoscopic outputs
//
// Matches pipeline descriptor sets: cameraSet (0), particleRenderSet (1)
// No 3rd descriptor set needed — elapsed time derived from particle life
//

struct Particle {
    vec4 position;   // xyz = position, w = life
    vec4 velocity;   // xyz = velocity, w = mass
};

// set 0 = cameraSet
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;    // xyz = camera world position
    vec4 params;      // x = near, y = far, z = fov, w = aspect
} camera;

// set 1 = particleRenderSet (SSBO)
layout(std430, set = 1, binding = 0) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(location = 0) out float outLife;
layout(location = 1) out float outSpeed;
layout(location = 2) flat out int outParticleID;
layout(location = 3) out float outElapsedTime;
layout(location = 4) out vec3 outWorldPos;

void main() {
    Particle p = particles[gl_VertexIndex];

    // Transform to clip space
    vec4 viewPos = camera.view * vec4(p.position.xyz, 1.0);
    gl_Position = camera.proj * viewPos;

    // Point size: enlarged, depth-scaled
    float lifeFactor = clamp(p.position.w * 0.5, 0.0, 1.0);
    float speed = length(p.velocity.xyz);
    float speedSize = 1.0 + clamp(speed / 5.0, 0.0, 1.5);
    float depthFactor = 1.0 / max(abs(viewPos.z), 0.1);
    gl_PointSize = clamp(60.0 * lifeFactor * speedSize * depthFactor, 3.0, 48.0);

    // Pass to fragment shader
    outLife = p.position.w;
    outSpeed = speed;
    outParticleID = gl_VertexIndex;
    // Approximate elapsed time from particle age (max life ~5s, so 5-life = age)
    outElapsedTime = 5.0 - p.position.w;
    outWorldPos = p.position.xyz;
}
