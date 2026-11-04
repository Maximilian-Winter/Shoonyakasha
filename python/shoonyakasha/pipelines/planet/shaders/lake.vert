#version 450
//
// The lakes' vertex shader: the terrain's chunks again, morphed as the
// terrain's are (morph.glsl). A chunk of the planet that a lake's water
// comes within reach of (lakes.glsl's lakeNear, from the middle of its
// unmorphed grid and half its diagonal) has each vertex moved along its
// direction to that lake's water level, a skirt vertex onto its edge
// vertex (the flat water needs no skirts), and hands the lake's index on;
// every vertex of any other
// chunk goes to one point outside the view, so its triangles cover nothing.
// Positions are jittered for the TAA pass (camera.glsl's jittered).
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
#include "terrain.glsl"
#include "lakes.glsl"

layout(location = 0) out vec3 fragRadial;
layout(location = 1) out vec3 fragRelativePos;
layout(location = 2) out float fragHorizon;
layout(location = 3) flat out int fragLake;

void main() {
    ChunkInstance chunk = chunks.instances[gl_InstanceIndex];
    int lake = -1;
    if (int(chunk.morph.z) == 0) {
        uint base = uint(gl_InstanceIndex) * uint(MORPH_VERTICES);
        ChunkVertex first = pool.vertices[base];
        ChunkVertex last = pool.vertices[base + uint(MORPH_GRID - 1)];
        ChunkVertex middle = pool.vertices[base + uint((MORPH_QUADS / 2) * MORPH_ROW + MORPH_QUADS / 2)];
        lake = lakeNear(middle.radialHeight.xyz, 0.5 * length(last.positionU.xyz - first.positionU.xyz));
    }
    fragLake = lake;
    if (lake < 0) {
        fragRadial = vec3(0.0, 0.0, 1.0);
        fragRelativePos = vec3(0.0);
        fragHorizon = 0.0;
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    int k = gl_VertexIndex;
    if (k >= MORPH_GRID) {
        ivec2 ij = morphSkirtCorner((k - MORPH_GRID) / MORPH_ROW, (k - MORPH_GRID) % MORPH_ROW);
        k = ij.y * MORPH_ROW + ij.x;
    }
    ChunkVertex v = morphedVertex(uint(k), uint(gl_InstanceIndex), chunk.origin.xyz, chunk.morph);
    float level = terrain.lakes[lake].w;
    vec3 relativePos = chunk.origin.xyz + v.positionU.xyz - v.radialHeight.xyz * (v.radialHeight.w - level);
    fragRadial = v.radialHeight.xyz;
    fragRelativePos = relativePos;
    fragHorizon = v.horizon.x;
    gl_Position = jittered(camera.proj * camera.view * vec4(relativePos, 1.0));
}
