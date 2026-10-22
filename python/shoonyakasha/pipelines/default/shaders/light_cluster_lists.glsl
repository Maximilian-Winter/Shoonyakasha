// light_cluster_lists.glsl - shared by light_cluster_count.comp and
// light_clusters.comp, which list the lights of the clusters (clusters.glsl)
// in two steps.
//
// The lists share one buffer, ClusterLightIndices. LightClusterCount stores
// how many lights each cluster wants in row 0 of the R32_UINT image (one
// column per cluster) and adds them up in ClusterLightCount.wanted.
// LightClusters then gives each cluster room for all of them, or, when the
// total does not fit, the same share of the buffer as of the total; takes
// the room from ClusterLightCount.used; writes the lights nearest the
// cluster that fit; and leaves the count in row 0 and the offset in row 1.
// Column CLUSTER_COUNT lists the directional lights, which reach everything.
// LightClusterReset zeroes the two counts first.

#ifndef DEFAULT_LIGHT_CLUSTER_LISTS_GLSL
#define DEFAULT_LIGHT_CLUSTER_LISTS_GLSL

#include "common.glsl"
#include "clusters.glsl"

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, r32ui) uniform uimage2D clusters;
layout(set = 0, binding = 1, std430) writeonly buffer ClusterLightIndices { uint clusterLightIndices[]; };
layout(set = 0, binding = 2, std430) buffer ClusterLightCount {
    uint wanted;   // what every cluster wants, together
    uint used;     // room taken so far
} counts;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Lights { DEFAULT_LIGHTS_BLOCK };
DEFAULT_SCENE_LIGHTS(2, 1)

// View-space point on the ray through NDC xy, at view depth -z.
vec3 onRay(vec2 ndc, float depth) {
    vec4 p = camera.invProj * vec4(ndc, 1.0, 1.0);
    vec3 direction = p.xyz / p.w;
    return direction * (depth / -direction.z);
}

// The view-space box of cluster `cluster`: its tile's corners at its
// slice's two depths.
void clusterBox(uint cluster, out vec3 lo, out vec3 hi) {
    lo = vec3(1e30);
    hi = vec3(-1e30);
    uvec3 c = uvec3(cluster % CLUSTER_X, (cluster / CLUSTER_X) % CLUSTER_Y, cluster / (CLUSTER_X * CLUSTER_Y));
    float nearPlane = camera.params.x, farPlane = camera.params.y;
    float d0 = clusterSliceDepth(c.z, nearPlane, farPlane);
    float d1 = clusterSliceDepth(c.z + 1u, nearPlane, farPlane);
    vec2 ndc0 = vec2(c.xy) / vec2(CLUSTER_X, CLUSTER_Y) * 2.0 - 1.0;
    vec2 ndc1 = vec2(c.xy + 1u) / vec2(CLUSTER_X, CLUSTER_Y) * 2.0 - 1.0;
    for (int k = 0; k < 8; ++k) {
        vec2 ndc = vec2((k & 1) != 0 ? ndc1.x : ndc0.x, (k & 2) != 0 ? ndc1.y : ndc0.y);
        vec3 p = onRay(ndc, (k & 4) != 0 ? d1 : d0);
        lo = min(lo, p);
        hi = max(hi, p);
    }
}

// Whether light i belongs in the cluster with view-space box lo..hi, and
// how far its centre is from the box (0 inside; 0 for directional lights,
// which belong only in the directional column).
bool belongs(uint i, bool directionalColumn, vec3 lo, vec3 hi, out float distance) {
    distance = 0.0;
    bool directional = sceneLights[i].positionType.w < 0.5;
    if (directionalColumn || directional) return directionalColumn && directional;
    float range = sceneLights[i].directionRange.w;
    vec3 center = (camera.view * vec4(sceneLights[i].positionType.xyz, 1.0)).xyz;
    vec3 nearest = clamp(center, lo, hi);
    distance = length(nearest - center);
    return range <= 0.0 || distance <= range;   // no range: everywhere
}

#endif
