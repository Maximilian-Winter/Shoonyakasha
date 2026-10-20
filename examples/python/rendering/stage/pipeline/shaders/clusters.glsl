// clusters.glsl - the light cluster grid, shared by light_clusters.comp,
// which fills it, and lights.glsl, which reads it. The grid's size is also in
// pipeline.json: the lightClusters image and the LightClusters dispatch.

#ifndef DEFAULT_CLUSTERS_GLSL
#define DEFAULT_CLUSTERS_GLSL

#define CLUSTER_X 16u
#define CLUSTER_Y 9u
#define CLUSTER_Z 24u
#define CLUSTER_COUNT 3456   // CLUSTER_X * CLUSTER_Y * CLUSTER_Z
#define MAX_PER_CLUSTER 63   // the image has 64 rows: a count and 63 lights

// View depth where slice `slice` begins; slices are spaced logarithmically.
float clusterSliceDepth(uint slice, float nearPlane, float farPlane) {
    return nearPlane * pow(farPlane / nearPlane, float(slice) / float(CLUSTER_Z));
}

// The cluster holding a fragment at screen uv and view depth.
uint clusterIndex(vec2 uv, float viewDepth, float nearPlane, float farPlane) {
    uvec2 tile = min(uvec2(uv * vec2(CLUSTER_X, CLUSTER_Y)), uvec2(CLUSTER_X - 1u, CLUSTER_Y - 1u));
    float t = log(max(viewDepth, nearPlane) / nearPlane) / log(farPlane / nearPlane);
    uint slice = min(uint(max(t, 0.0) * float(CLUSTER_Z)), CLUSTER_Z - 1u);
    return tile.x + tile.y * CLUSTER_X + slice * CLUSTER_X * CLUSTER_Y;
}

#endif
