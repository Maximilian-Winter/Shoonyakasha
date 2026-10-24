#version 450
//
// The ocean's vertex shader: the terrain's chunks again, morphed as the
// terrain's are (morph.glsl), each vertex moved down (or up) along its
// direction to sea level, the sphere of the planet's radius. A skirt vertex
// keeps its skirt depth below sea level.
//

#define CAMERA_SET 0
#include "camera.glsl"

struct ChunkInstance {
    vec4 origin;           // xyz: the chunk's centre minus the camera's position
    vec4 colour;
    vec4 morph;            // x: the parent's split distance (m); y: the levels above the chunk; z: its body
};

layout(std430, set = 1, binding = 0) readonly buffer ChunkInstances {
    ChunkInstance instances[];
} chunks;

#include "morph.glsl"

layout(location = 0) out vec3 fragRadial;
layout(location = 1) out vec3 fragRelativePos;
layout(location = 2) out float fragHorizon;

void main() {
    ChunkInstance chunk = chunks.instances[gl_InstanceIndex];
    ChunkVertex v = morphedVertex(uint(gl_VertexIndex), uint(gl_InstanceIndex), chunk.origin.xyz, chunk.morph);
    vec3 relativePos = chunk.origin.xyz + v.positionU.xyz - v.radialHeight.xyz * v.radialHeight.w;
    fragRadial = v.radialHeight.xyz;
    fragRelativePos = relativePos;
    fragHorizon = v.horizon.x;
    gl_Position = camera.proj * camera.view * vec4(relativePos, 1.0);
}
