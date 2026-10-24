#version 450
//
// The terrain's vertex shader. Each indirect draw is one chunk: its
// vertices come from ChunkPool (vertexOffset picks the slot), and its
// firstInstance is the slot, which indexes ChunkInstances for the chunk's
// centre relative to the camera and its colour.
//

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;
} camera;

struct ChunkInstance {
    vec4 origin;           // xyz: the chunk's centre minus the camera's position
    vec4 colour;           // rgb: albedo, or r < 0 for the terrain's own colours; a: grid strength
};

layout(std430, set = 1, binding = 0) readonly buffer ChunkInstances {
    ChunkInstance instances[];
} chunks;

layout(location = 0) in vec4 inPositionU;
layout(location = 1) in vec4 inNormalV;
layout(location = 2) in vec4 inRadialHeight;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec4 fragColour;
layout(location = 2) out vec2 fragFaceUv;
layout(location = 3) out vec4 fragRadialHeight;

void main() {
    ChunkInstance chunk = chunks.instances[gl_InstanceIndex];
    vec3 relativePos = chunk.origin.xyz + inPositionU.xyz;
    fragNormal = inNormalV.xyz;
    fragColour = chunk.colour;
    fragFaceUv = vec2(inPositionU.w, inNormalV.w);
    fragRadialHeight = inRadialHeight;
    gl_Position = camera.proj * camera.view * vec4(relativePos, 1.0);
}
