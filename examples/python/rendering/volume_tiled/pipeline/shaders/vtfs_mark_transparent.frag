#version 450
// VTFS: flag the clusters of transparent (blended) fragments in front of
// the opaque scene. Drawn with the transparent geometry against the
// prepass depth; early depth testing keeps hidden fragments from marking.
// Writes no colour.

#include "common.glsl"
#include "vtfs.glsl"

layout(early_fragment_tests) in;

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(std430, set = 2, binding = 0) writeonly buffer VtfsClusterFlags { uint clusterFlags[]; };

void main() {
    Grid g = vtfsGrid(camera.proj, camera.params.x, camera.params.y, camera.resolution);
    clusterFlags[vtfsCluster(g, camera.invProj, uvec2(gl_FragCoord.xy), gl_FragCoord.z)] = 1u;
}
