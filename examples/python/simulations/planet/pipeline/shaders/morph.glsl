// Continuous level of detail (CDLOD morphing) for the chunk vertex shaders.
//
// A chunk's ancestor k levels up has its grid vertices at this chunk's
// vertices every 2^k (k up to 4, a whole chunk), and splits each of its
// quads along the diagonal from its lowest to its highest corner, as
// cube_sphere.chunk_mesh does. Its surface at any vertex of this chunk is
// therefore a weighted sum of three of this chunk's vertices
// (strideWeights, as cube_sphere.stride_weights).
//
// A vertex morphs towards its parent's surface by the factor f1, then from
// there towards its grandparent's by f2, and so on, where fk is
// smoothstep(MORPH_START * Sk, Sk, distance from the camera to the
// vertex) and Sk is the k-th ancestor's split distance, S1 * 2^(k - 1)
// (ChunkInstance.morph: x S1, y the levels the chunk has above it; the
// roots have none and do not morph). A skirt vertex keeps its offset from
// its edge vertex.
//
// A parent splits when the camera comes within S1 of its bounding sphere,
// which holds every vertex of the chunk, so the chunk appears with every
// vertex on the parent's surface; it merges further out, where they are
// again. A chunk's edge beside a coarser neighbour is at least S1 away,
// so it lies on the neighbour's grid; and as the neighbour morphs towards
// its own ancestors, by the same factors of the same distances, the edge
// follows it there.
//
// Declares ChunkPool, read-only, at set 1, binding 1 (chunkInstanceSet in
// pipeline.json).

const int MORPH_QUADS = 16;
const int MORPH_ROW = MORPH_QUADS + 1;
const int MORPH_GRID = MORPH_ROW * MORPH_ROW;
const int MORPH_VERTICES = MORPH_GRID + 4 * MORPH_ROW;
const int MORPH_LEVELS = 4;
const float MORPH_START = 0.8;

struct ChunkVertex {
    vec4 positionU;        // xyz: position relative to the chunk's centre; w: face u
    vec4 normalV;          // xyz: surface normal; w: face v
    vec4 radialHeight;     // xyz: direction from the planet's centre; w: height (m)
};

layout(std430, set = 1, binding = 1) readonly buffer ChunkPool {
    ChunkVertex vertices[];
} pool;

// Grid coordinates of skirt vertex t of edge e (as chunk_build.comp's skirtCorner).
ivec2 morphSkirtCorner(int e, int t) {
    if (e == 0) return ivec2(t, 0);
    if (e == 1) return ivec2(MORPH_QUADS, t);
    if (e == 2) return ivec2(MORPH_QUADS - t, MORPH_QUADS);
    return ivec2(0, MORPH_QUADS - t);
}

// The three grid vertices and their weights that give the surface of the
// grid taken every `stride` vertices at grid vertex `ij`.
void strideWeights(ivec2 ij, int stride, out ivec2 v0, out ivec2 v1, out ivec2 v2, out vec3 w) {
    ivec2 lo = min((ij / stride) * stride, ivec2(MORPH_QUADS - stride));
    vec2 f = vec2(ij - lo) / float(stride);
    v0 = lo;
    if (f.x >= f.y) {
        v1 = lo + ivec2(stride, 0);
        v2 = lo + ivec2(stride);
        w = vec3(1.0 - f.x, f.x - f.y, f.y);
    } else {
        v1 = lo + ivec2(stride);
        v2 = lo + ivec2(0, stride);
        w = vec3(1.0 - f.y, f.x, f.y - f.x);
    }
}

float morphFactor(float distance, float splitDistance) {
    return smoothstep(MORPH_START * splitDistance, splitDistance, distance);
}

// The vertex `vertexIndex` (gl_VertexIndex) of the chunk in `slot` whose
// centre is `origin` relative to the camera, morphed: its position
// (relative to the chunk's centre), normal, and direction and height.
ChunkVertex morphedVertex(uint vertexIndex, uint slot, vec3 origin, vec4 morph) {
    ChunkVertex own = pool.vertices[vertexIndex];
    int levels = min(MORPH_LEVELS, int(morph.y));
    if (levels <= 0 || morph.x <= 0.0) return own;
    uint base = slot * uint(MORPH_VERTICES);
    int k = int(vertexIndex - base);
    ivec2 ij = k < MORPH_GRID ? ivec2(k % MORPH_ROW, k / MORPH_ROW)
                              : morphSkirtCorner((k - MORPH_GRID) / MORPH_ROW, (k - MORPH_GRID) % MORPH_ROW);
    ChunkVertex edge = pool.vertices[base + uint(ij.y * MORPH_ROW + ij.x)];
    float distance = length(origin + edge.positionU.xyz);

    vec3 position = edge.positionU.xyz, normal = edge.normalV.xyz, radial = edge.radialHeight.xyz;
    float height = edge.radialHeight.w;
    for (int level = 1; level <= levels; ++level) {
        float f = morphFactor(distance, morph.x * float(1 << (level - 1)));
        if (f <= 0.0) break;                      // the factors fall with the level
        ivec2 v0, v1, v2;
        vec3 w;
        strideWeights(ij, 1 << level, v0, v1, v2, w);
        ChunkVertex a = pool.vertices[base + uint(v0.y * MORPH_ROW + v0.x)];
        ChunkVertex b = pool.vertices[base + uint(v1.y * MORPH_ROW + v1.x)];
        ChunkVertex c = pool.vertices[base + uint(v2.y * MORPH_ROW + v2.x)];
        position = mix(position, w.x * a.positionU.xyz + w.y * b.positionU.xyz + w.z * c.positionU.xyz, f);
        normal = mix(normal, w.x * a.normalV.xyz + w.y * b.normalV.xyz + w.z * c.normalV.xyz, f);
        radial = mix(radial, w.x * a.radialHeight.xyz + w.y * b.radialHeight.xyz + w.z * c.radialHeight.xyz, f);
        height = mix(height, w.x * a.radialHeight.w + w.y * b.radialHeight.w + w.z * c.radialHeight.w, f);
    }
    ChunkVertex morphed = own;
    // A skirt vertex moves with its edge vertex.
    morphed.positionU.xyz = own.positionU.xyz + (position - edge.positionU.xyz);
    morphed.normalV.xyz = normalize(normal);
    morphed.radialHeight = vec4(normalize(radial), height);
    return morphed;
}
