#version 450
//
// The terrain into one cascade of the sun's shadow map (depth only): each
// indirect draw is one chunk, morphed as the terrain's vertex shader
// morphs it (morph.glsl), placed relative to the camera and taken to the
// cascade's clip space. The pass instance gives the cascade.
//

layout(set = 0, binding = 0) uniform ShadowParams {
    mat4 viewProj[4];
    vec4 splits;
    vec4 texel;
    vec4 settings;
} shadow;

struct ChunkInstance {
    vec4 origin;           // xyz: the chunk's centre minus the camera's position
    vec4 colour;
    vec4 morph;            // x: the parent's split distance (m); y: the levels above the chunk; z: its body
};

layout(std430, set = 1, binding = 0) readonly buffer ChunkInstances {
    ChunkInstance instances[];
} chunks;

#include "morph.glsl"

layout(push_constant) uniform ShadowPass {
    uint cascade;
} pass;

void main() {
    ChunkInstance chunk = chunks.instances[gl_InstanceIndex];
    ChunkVertex v = morphedVertex(uint(gl_VertexIndex), uint(gl_InstanceIndex), chunk.origin.xyz, chunk.morph);
    gl_Position = shadow.viewProj[pass.cascade] * vec4(chunk.origin.xyz + v.positionU.xyz, 1.0);
}
