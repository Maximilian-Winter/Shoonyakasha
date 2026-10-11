// vtfs.glsl - Volume Tiled Forward Shading (van Oosten 2015): the cluster
// grid, the buffers' capacities, and the helpers every VTFS pass shares.
//
// The grid is the paper's: screen tiles of VTFS_TILE pixels, cut in depth
// into slices whose thickness grows with distance so that each volume tile
// is roughly a cube (Eq. 8.1, 8.2). Its size follows the render resolution
// and the camera's near plane, far plane and field of view, and is worked
// out from the camera block by every pass that needs it.
//
// The capacities below are also the elementCount of the buffers in
// pipeline.json, and the worst-case dispatch sizes there.

#ifndef VTFS_GLSL
#define VTFS_GLSL

#define VTFS_TILE 64u                   // screen tile size in pixels
#define VTFS_MAX_SLICES 256u            // depth slices at most
#define VTFS_MAX_CLUSTERS 524288u       // 2^19: 60 x 34 tiles (3840 x 2160) x 256 slices
#define VTFS_MAX_LIGHTS 1048576u        // 2^20
#define VTFS_MAX_INDICES 8388608u       // 2^23 entries of the global light index list
#define VTFS_MAX_PER_CLUSTER 1024u      // lights one cluster can list
#define VTFS_BVH_LEVELS 4u              // 32^4 = VTFS_MAX_LIGHTS leaves
#define VTFS_BVH_NODES 33825u           // 1 + 32 + 1024 + 32768
#define VTFS_REDUCE_GROUPS 512u         // partial light bounds; the total is at [VTFS_REDUCE_GROUPS]
#define VTFS_SORT_CHUNK 256u            // keys the radix sort orders per workgroup
#define VTFS_MERGE_PASSES 12u           // 256 << 12 = VTFS_MAX_LIGHTS

// scene.custom.vtfs.* and the time (pipeline.json "Vtfs")
#define VTFS_BLOCK          \
    uint lightCount;        \
    float lightRange;       \
    float lightIntensity;   \
    float time;             \
    vec4 boxMin;            \
    vec4 boxMax;            \
    float motion;           \
    float heatScale;

// Overflow flags in VtfsCounters.overflow.
#define VTFS_OVERFLOW_CLUSTER 1u        // a cluster had more than VTFS_MAX_PER_CLUSTER lights
#define VTFS_OVERFLOW_INDICES 2u        // the light index list was full

struct AABB {
    vec4 lo;    // xyz; w unused
    vec4 hi;
};

AABB emptyAABB() {
    return AABB(vec4(vec3(3.0e38), 0.0), vec4(vec3(-3.0e38), 0.0));
}

// ── The grid ────────────────────────────────────────────────────

struct Grid {
    uvec3 dim;      // tiles across, tiles down, slices
    float near;     // view depth where slice 0 begins
    float nearK;    // ratio between one slice's far and near depth
    float logNearK; // log(nearK)
};

// The grid for a camera with projection `proj`, planes `nearPlane` and
// `farPlane`, rendering at `resolution` pixels.
Grid vtfsGrid(mat4 proj, float nearPlane, float farPlane, vec2 resolution) {
    Grid g;
    g.dim.xy = uvec2(ceil(resolution / float(VTFS_TILE)));
    g.near = nearPlane;
    // Eq. 8.2: tanTheta is the tangent of half the vertical field of view.
    float tanTheta = 1.0 / abs(proj[1][1]);
    g.nearK = 1.0 + 2.0 * tanTheta / float(g.dim.y);
    float ratio = log(farPlane / nearPlane);
    uint slices = uint(floor(ratio / log(g.nearK)));
    // Past the capacity, fewer and thicker slices still reach the far plane.
    uint fit = min(VTFS_MAX_SLICES, VTFS_MAX_CLUSTERS / (g.dim.x * g.dim.y));
    if (slices > fit) {
        slices = fit;
        g.nearK = exp(ratio / float(slices));
    }
    g.dim.z = max(slices, 1u);
    g.logNearK = log(g.nearK);
    return g;
}

uint vtfsClusterCount(Grid g) {
    return g.dim.x * g.dim.y * g.dim.z;
}

uint vtfsClusterIndex(Grid g, uvec3 c) {
    return c.x + g.dim.x * (c.y + g.dim.y * c.z);
}

uvec3 vtfsClusterCoord(Grid g, uint index) {
    return uvec3(index % g.dim.x, (index / g.dim.x) % g.dim.y, index / (g.dim.x * g.dim.y));
}

// View depth (positive, in front of the camera) of a depth-buffer value. The
// projection is OpenGL style and the buffer stores the NDC depth as is
// (common.glsl), so it goes back through the inverse projection.
float vtfsViewDepth(mat4 invProj, float depth) {
    vec4 p = invProj * vec4(0.0, 0.0, depth, 1.0);
    return -p.z / p.w;
}

// The cluster holding the pixel `pixel` (integer coordinates) whose depth
// buffer holds `depth`. Marking and shading call this with the same inputs,
// so a shaded pixel finds the cluster its depth marked.
uint vtfsCluster(Grid g, mat4 invProj, uvec2 pixel, float depth) {
    float z = vtfsViewDepth(invProj, depth);
    float k = floor(log(max(z, g.near) / g.near) / g.logNearK);
    uvec3 c = uvec3(min(pixel / VTFS_TILE, g.dim.xy - 1u), uint(clamp(k, 0.0, float(g.dim.z - 1u))));
    return vtfsClusterIndex(g, c);
}

// ── Lights ──────────────────────────────────────────────────────

// Sphere against box: the squared distance from the centre to the box.
bool sphereTouchesAABB(vec3 centre, float radius, AABB box) {
    vec3 d = max(max(box.lo.xyz - centre, centre - box.hi.xyz), 0.0);
    return dot(d, d) <= radius * radius;
}

bool aabbTouchesAABB(AABB a, AABB b) {
    return all(lessThanEqual(a.lo.xyz, b.hi.xyz)) && all(lessThanEqual(b.lo.xyz, a.hi.xyz));
}

// 30-bit Morton code of a point in the unit cube: 10 bits per axis,
// interleaved x, y, z from the lowest bit up.
uint spreadBits10(uint v) {
    v &= 0x3FFu;
    v = (v | (v << 16)) & 0x030000FFu;
    v = (v | (v << 8)) & 0x0300F00Fu;
    v = (v | (v << 4)) & 0x030C30C3u;
    v = (v | (v << 2)) & 0x09249249u;
    return v;
}

uint mortonCode(vec3 unit) {
    uvec3 q = uvec3(clamp(unit, 0.0, 1.0) * 1023.0);
    return spreadBits10(q.x) | (spreadBits10(q.y) << 1) | (spreadBits10(q.z) << 2);
}

// ── The light BVH ───────────────────────────────────────────────
// A 32-ary tree over the lights sorted by Morton code, stored level by
// level, root first. Only inner nodes are stored: the children of a node on
// the lowest level are the 32 sorted lights it bounds.

// Levels for `count` lights: 32^levels >= count.
uint bvhLevels(uint count) {
    uint levels = 1u;
    uint capacity = 32u;
    while (capacity < count && levels < VTFS_BVH_LEVELS) {
        capacity *= 32u;
        ++levels;
    }
    return levels;
}

// Index of the first node of `level`: (32^level - 1) / 31.
uint bvhFirstNode(uint level) {
    return ((1u << (5u * level)) - 1u) / 31u;
}

// Nodes on `level` that bound at least one of `count` lights.
uint bvhLevelNodes(uint level, uint levels, uint count) {
    uint span = 1u << (5u * (levels - level));     // lights under one node of the level
    return (count + span - 1u) / span;
}

#endif
