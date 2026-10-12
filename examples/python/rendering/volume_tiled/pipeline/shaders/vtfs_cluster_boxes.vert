#version 450
// VTFS debug overlay: the boxes of the active clusters, as lines. Drawn with
// draw_indirect, one instance per active cluster and 24 vertices each (12
// edges), from the arguments vtfs_indirect_args.comp wrote.

#include "common.glsl"
#include "vtfs.glsl"

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(std430, set = 0, binding = 1) readonly buffer VtfsUniqueClusters { uint uniqueClusters[]; };
layout(std430, set = 0, binding = 2) readonly buffer VtfsClusterAABBs { AABB clusterAABBs[]; };

layout(location = 0) out vec3 fragColor;

// Corners 0 to 7: bit 0 x, bit 1 y, bit 2 z from the box's low to high side.
const uint EDGES[24] = uint[](0u, 1u, 1u, 3u, 3u, 2u, 2u, 0u,
                              4u, 5u, 5u, 7u, 7u, 6u, 6u, 4u,
                              0u, 4u, 1u, 5u, 2u, 6u, 3u, 7u);

void main() {
    uint cluster = uniqueClusters[gl_InstanceIndex];
    AABB box = clusterAABBs[cluster];
    uint corner = EDGES[gl_VertexIndex];
    vec3 t = vec3(corner & 1u, (corner >> 1) & 1u, (corner >> 2) & 1u);
    vec3 viewPos = mix(box.lo.xyz, box.hi.xyz, t);
    gl_Position = camera.proj * vec4(viewPos, 1.0);
    gl_Position.xy += taaJitter(camera.frame, camera.taa, camera.resolution) * gl_Position.w;

    uint h = cluster * 2654435761u;
    fragColor = vec3((h >> 8) & 255u, (h >> 16) & 255u, (h >> 24) & 255u) / 255.0;
}
