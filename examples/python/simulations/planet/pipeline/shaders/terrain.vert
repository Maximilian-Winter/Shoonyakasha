#version 450
//
// The terrain's vertex shader. Each indirect draw is one chunk: its
// vertices come from ChunkPool (vertexOffset picks the slot), and its
// firstInstance is the slot, which indexes ChunkInstances for the chunk's
// centre relative to the camera, its colour and what morphing needs.
// Vertices are morphed towards their ancestors' surfaces by distance
// (morph.glsl), reading the ancestors' vertices from ChunkPool.
//

#define CAMERA_SET 0
#include "camera.glsl"

struct ChunkInstance {
    vec4 origin;           // xyz: the chunk's centre minus the camera's position
    vec4 colour;           // rgb: albedo, or r < 0 for the terrain's own colours; a: grid strength
    vec4 morph;            // x: the parent's split distance (m); y: the levels above the chunk
};

layout(std430, set = 1, binding = 0) readonly buffer ChunkInstances {
    ChunkInstance instances[];
} chunks;

#include "morph.glsl"

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec4 fragColour;
layout(location = 2) out vec2 fragFaceUv;
layout(location = 3) out vec4 fragRadialHeight;
layout(location = 4) out vec3 fragRelativePos;
layout(location = 5) out float fragHorizon;

void main() {
    ChunkInstance chunk = chunks.instances[gl_InstanceIndex];
    ChunkVertex v = morphedVertex(uint(gl_VertexIndex), uint(gl_InstanceIndex), chunk.origin.xyz, chunk.morph);
    vec3 relativePos = chunk.origin.xyz + v.positionU.xyz;
    fragNormal = v.normalV.xyz;
    fragColour = chunk.colour;
    fragFaceUv = vec2(v.positionU.w, v.normalV.w);
    fragRadialHeight = v.radialHeight;
    fragRelativePos = relativePos;
    fragHorizon = v.horizon.x;
    gl_Position = camera.proj * camera.view * vec4(relativePos, 1.0);
}
