// vtfs_compute.glsl - the bindings of pipeline.json's vtfsComputeSet (set 0
// of every VTFS compute pass), as macros. A shader declares only the
// buffers it uses, each `readonly` where it only reads it: the frame graph
// orders the passes by which buffers each one writes.
//
//   VTFS_COUNTERS(restrict)
//   VTFS_LIGHTS_VIEW(readonly)

#ifndef VTFS_COMPUTE_GLSL
#define VTFS_COMPUTE_GLSL

#include "common.glsl"
#include "vtfs.glsl"

layout(set = 0, binding = 0) uniform Vtfs { VTFS_BLOCK } vtfs;
layout(set = 0, binding = 16) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

// What the passes found this frame; cleared by vtfs_reset.comp.
#define VTFS_COUNTERS(q) layout(std430, set = 0, binding = 1) q buffer VtfsCounters { \
    uint activeClusters;   /* entries of uniqueClusters */ \
    uint indexCount;       /* entries of lightIndexList */ \
    uint overflow;         /* VTFS_OVERFLOW_* */ \
    uint _countersPad; } counters;
// 1 where a visible pixel or transparent fragment lies in the cluster.
#define VTFS_CLUSTER_FLAGS(q) layout(std430, set = 0, binding = 2) q buffer VtfsClusterFlags { uint clusterFlags[]; };
// The flagged clusters, in no order; activeClusters of them.
#define VTFS_UNIQUE_CLUSTERS(q) layout(std430, set = 0, binding = 3) q buffer VtfsUniqueClusters { uint uniqueClusters[]; };
// Each cluster's view-space box.
#define VTFS_CLUSTER_AABBS(q) layout(std430, set = 0, binding = 4) q buffer VtfsClusterAABBs { AABB clusterAABBs[]; };
// Each cluster's lights: (offset into lightIndexList, count).
#define VTFS_LIGHT_GRID(q) layout(std430, set = 0, binding = 5) q buffer VtfsLightGrid { uvec2 lightGrid[]; };
#define VTFS_LIGHT_INDEX_LIST(q) layout(std430, set = 0, binding = 6) q buffer VtfsLightIndexList { uint lightIndexList[]; };
// Each light: world position and range; colour times intensity; view
// position and range.
#define VTFS_LIGHTS_WORLD(q) layout(std430, set = 0, binding = 7) q buffer VtfsLightsWorld { vec4 lightsWorld[]; };
#define VTFS_LIGHTS_COLOR(q) layout(std430, set = 0, binding = 8) q buffer VtfsLightsColor { vec4 lightsColor[]; };
#define VTFS_LIGHTS_VIEW(q) layout(std430, set = 0, binding = 9) q buffer VtfsLightsView { vec4 lightsView[]; };
// The lights' view-space bounds: VTFS_REDUCE_GROUPS partial boxes, then the
// whole at [VTFS_REDUCE_GROUPS].
#define VTFS_LIGHT_BOUNDS(q) layout(std430, set = 0, binding = 10) q buffer VtfsLightBounds { AABB lightBounds[]; };
// Morton codes and light indices, sorted together; the sort ping-pongs
// between A and B and ends in A.
#define VTFS_KEYS_A(q) layout(std430, set = 0, binding = 11) q buffer VtfsKeysA { uint keysA[]; };
#define VTFS_KEYS_B(q) layout(std430, set = 0, binding = 12) q buffer VtfsKeysB { uint keysB[]; };
#define VTFS_VALUES_A(q) layout(std430, set = 0, binding = 13) q buffer VtfsValuesA { uint valuesA[]; };
#define VTFS_VALUES_B(q) layout(std430, set = 0, binding = 14) q buffer VtfsValuesB { uint valuesB[]; };
// Arguments for the passes that follow the number of active clusters
// (vtfs_indirect_args.comp): the light assignment's dispatch_indirect at
// offset 0, the cluster box overlay's draw_indirect at offset 16.
#define VTFS_INDIRECT_ARGS(q) layout(std430, set = 0, binding = 17) q buffer VtfsIndirectArgs {     uvec3 assignGroups;    /* VkDispatchIndirectCommand */     uvec4 boxDraw;         /* VkDrawIndirectCommand */ } indirectArgs;
// The light BVH's inner nodes (vtfs.glsl).
#define VTFS_BVH_NODES_BUFFER(q) layout(std430, set = 0, binding = 15) q buffer VtfsBvhNodes { AABB bvhNodes[]; };

Grid cameraGrid() {
    return vtfsGrid(camera.proj, camera.params.x, camera.params.y, camera.resolution);
}

uint lightCount() {
    return min(vtfs.lightCount, VTFS_MAX_LIGHTS);
}

#endif
