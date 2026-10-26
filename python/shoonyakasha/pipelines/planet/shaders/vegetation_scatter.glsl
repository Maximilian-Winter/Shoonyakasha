// The declarations and steps the vegetation's scatter passes share
// (vegetation_scatter.comp, grass_scatter.comp): the sets and buffers, the
// chunk a workgroup places plants on (one per draw of ChunkDraws), the cells
// of a cube face's grid that chunk holds, a cell's candidate (a point
// jittered within the cell by a hash of it, kept when it lies in the chunk
// and within range), the ground under it from the chunk's grid unmorphed,
// its place on the surface as drawn (morphed), and claiming an instance.

layout(local_size_x = 64) in;

layout(set = 0, binding = 0) uniform TerrainParams {
    vec4 continent;
    vec4 mountains;
    vec4 hills;
    vec4 seed;
    vec4 site;
    vec4 siteShape;
    vec4 detail;
} terrain;

#define VEGETATION_SET 0
#define VEGETATION_PARAMS 1
#include "vegetation.glsl"

layout(std430, set = 0, binding = 2) readonly buffer ChunkDraws {
    uint words[];
} chunkDraws;

layout(std430, set = 0, binding = 3) writeonly buffer VegetationInstances {
    vec4 data[];
} instances;

layout(std430, set = 0, binding = 4) buffer VegetationDraws {
    uint words[];
} draws;

struct ChunkInstance {
    vec4 origin;           // xyz: the chunk's centre minus the camera's position
    vec4 colour;
    vec4 morph;            // x: the parent's split distance; y: the levels above the chunk; z: its body
};

layout(std430, set = 1, binding = 0) readonly buffer ChunkInstances {
    ChunkInstance instances[];
} chunks;

#include "terrain.glsl"
#include "biome.glsl"
#include "morph.glsl"

// The cube face whose normal is nearest the unit direction `d` (cube_sphere.FACES' order).
int faceOf(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0 : 1;
    if (a.y >= a.z) return d.y > 0.0 ? 2 : 3;
    return d.z > 0.0 ? 4 : 5;
}

uint nextHash(inout uint h) {
    h = h * 747796405u + 2891336453u;
    uint w = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    return (w >> 22u) ^ w;
}

float nextRandom(inout uint h) {
    return float(nextHash(h) & 0xFFFFFFu) / 16777216.0;
}

// The grid vertex (i, j) of the chunk at `base`, unmorphed.
ChunkVertex gridVertex(uint base, int i, int j) {
    return pool.vertices[base + uint(j * MORPH_ROW + i)];
}

// The three grid vertices around (fi, fj) (grid coordinates) and their
// weights, split along each quad's rising diagonal as the chunk's mesh is.
void gridTriangle(float fi, float fj, out ivec2 a, out ivec2 b, out ivec2 c, out vec3 w) {
    int i = clamp(int(floor(fi)), 0, MORPH_QUADS - 1);
    int j = clamp(int(floor(fj)), 0, MORPH_QUADS - 1);
    float x = clamp(fi - float(i), 0.0, 1.0), y = clamp(fj - float(j), 0.0, 1.0);
    a = ivec2(i, j);
    if (x >= y) {
        b = ivec2(i + 1, j);
        c = ivec2(i + 1, j + 1);
        w = vec3(1.0 - x, x - y, y);
    } else {
        b = ivec2(i + 1, j + 1);
        c = ivec2(i, j + 1);
        w = vec3(1.0 - y, x, y - x);
    }
}

// A chunk ChunkDraws draws, as the scatter passes need it.
struct ScatterChunk {
    uint slot;
    uint base;             // its first vertex in ChunkPool
    vec3 origin;           // the point its vertices are relative to, less the camera's position
    vec4 morph;
    float u0, v0, u1, v1;  // its face coordinates
    int face;
};

// The chunk of draw `group`, when there is one, it is the planet's (body
// 0), at least `minLevel`, and its nearest point is within `range`.
bool scatterChunk(uint group, float minLevel, float range, out ScatterChunk chunk) {
    if (group >= chunkDraws.words[0]) return false;
    chunk.slot = chunkDraws.words[4u + group * 5u + 4u];
    ChunkInstance instance = chunks.instances[chunk.slot];
    if (int(instance.morph.z) != 0 || instance.morph.y < minLevel) return false;
    chunk.base = chunk.slot * uint(MORPH_VERTICES);
    chunk.origin = instance.origin.xyz;
    chunk.morph = instance.morph;
    ChunkVertex first = gridVertex(chunk.base, 0, 0);
    ChunkVertex last = gridVertex(chunk.base, MORPH_QUADS, MORPH_QUADS);
    ChunkVertex middle = gridVertex(chunk.base, MORPH_QUADS / 2, MORPH_QUADS / 2);
    float halfDiagonal = 0.5 * length(last.positionU.xyz - first.positionU.xyz) + 50.0;
    if (length(chunk.origin + middle.positionU.xyz) - halfDiagonal > range) return false;
    chunk.u0 = first.positionU.w;
    chunk.v0 = first.normalV.w;
    chunk.u1 = last.positionU.w;
    chunk.v1 = last.normalV.w;
    chunk.face = faceOf(middle.radialHeight.xyz);
    return true;
}

// The cells of a face's `cells` x `cells` grid the chunk's face coordinates
// touch: from (cu0, cv0), `across` a row, `count` in all.
void scatterCells(ScatterChunk chunk, float cells, out int cu0, out int cv0, out uint across, out uint count) {
    cu0 = int(floor((chunk.u0 + 1.0) * 0.5 * cells));
    cv0 = int(floor((chunk.v0 + 1.0) * 0.5 * cells));
    int cu1 = int(ceil((chunk.u1 + 1.0) * 0.5 * cells)), cv1 = int(ceil((chunk.v1 + 1.0) * 0.5 * cells));
    across = uint(max(cu1 - cu0, 0));
    count = across * uint(max(cv1 - cv0, 0));
}

// A cell's candidate: its grid triangle and weights, its position relative
// to the chunk's centre on the unmorphed grid, its distance from the camera,
// and its hash after the two draws that jittered it.
struct Candidate {
    ivec2 a, b, c;
    vec3 w;
    vec3 position;
    float distance;
    uint hash;
};

// The candidate of cell k (of scatterCells' count); false when it lies in
// another chunk or beyond `range`. `salt` tells grids apart in the hash.
bool scatterCandidate(ScatterChunk chunk, float cells, int cu0, int cv0, uint across, uint k, int salt,
                      float range, out Candidate x) {
    int cu = cu0 + int(k % across), cv = cv0 + int(k / across);
    x.hash = terrainHash(ivec3(cu, cv, chunk.face + salt));
    float pu = -1.0 + 2.0 * (float(cu) + nextRandom(x.hash)) / cells;
    float pv = -1.0 + 2.0 * (float(cv) + nextRandom(x.hash)) / cells;
    if (pu < chunk.u0 || pu >= chunk.u1 || pv < chunk.v0 || pv >= chunk.v1) return false;
    float fi = (pu - chunk.u0) / (chunk.u1 - chunk.u0) * float(MORPH_QUADS);
    float fj = (pv - chunk.v0) / (chunk.v1 - chunk.v0) * float(MORPH_QUADS);
    gridTriangle(fi, fj, x.a, x.b, x.c, x.w);
    ChunkVertex va = gridVertex(chunk.base, x.a.x, x.a.y), vb = gridVertex(chunk.base, x.b.x, x.b.y);
    ChunkVertex vc = gridVertex(chunk.base, x.c.x, x.c.y);
    x.position = x.w.x * va.positionU.xyz + x.w.y * vb.positionU.xyz + x.w.z * vc.positionU.xyz;
    x.distance = length(chunk.origin + x.position);
    return x.distance <= range;
}

// The ground under a candidate on the unmorphed grid: its direction from
// the planet's centre, its height and the surface's normal.
void candidateGround(ScatterChunk chunk, Candidate x, out vec3 radial, out float height, out vec3 normal) {
    ChunkVertex va = gridVertex(chunk.base, x.a.x, x.a.y), vb = gridVertex(chunk.base, x.b.x, x.b.y);
    ChunkVertex vc = gridVertex(chunk.base, x.c.x, x.c.y);
    radial = normalize(x.w.x * va.radialHeight.xyz + x.w.y * vb.radialHeight.xyz + x.w.z * vc.radialHeight.xyz);
    height = x.w.x * va.radialHeight.w + x.w.y * vb.radialHeight.w + x.w.z * vc.radialHeight.w;
    normal = normalize(x.w.x * va.normalV.xyz + x.w.y * vb.normalV.xyz + x.w.z * vc.normalV.xyz);
}

// A candidate's position relative to the camera on the surface as drawn.
vec3 candidateDrawn(ScatterChunk chunk, Candidate x) {
    uint ia = chunk.base + uint(x.a.y * MORPH_ROW + x.a.x), ib = chunk.base + uint(x.b.y * MORPH_ROW + x.b.x);
    uint ic = chunk.base + uint(x.c.y * MORPH_ROW + x.c.x);
    ChunkVertex ma = morphedVertex(ia, chunk.slot, chunk.origin, chunk.morph);
    ChunkVertex mb = morphedVertex(ib, chunk.slot, chunk.origin, chunk.morph);
    ChunkVertex mc = morphedVertex(ic, chunk.slot, chunk.origin, chunk.morph);
    return chunk.origin + x.w.x * ma.positionU.xyz + x.w.y * mb.positionU.xyz + x.w.z * mc.positionU.xyz;
}

// The next instance of draw `draw` (an atomic add on its instance count in
// VegetationDraws), whose instances start at `first` and hold `capacity`;
// false, with the count given back, when they are full. `at` is the
// instance's first vec4 in VegetationInstances.
bool claimInstance(uint draw, uint first, uint capacity, out uint at) {
    uint index = atomicAdd(draws.words[draw * 5u + 1u], 1u);
    if (index >= capacity) {
        atomicAdd(draws.words[draw * 5u + 1u], 0xFFFFFFFFu);
        return false;
    }
    at = (first + index) * 2u;
    return true;
}
