// ircache.glsl - the irradiance cache: its buffers, the grid of cascades,
// how an entry's samples are stratified, its reservoirs, and the lookup that
// allocates entries and votes for where they trace from.
//
// The including shader defines IRCACHE_SET, the set of the "ircacheSet"
// layout in pipeline.json. Before including it, a shader that looks up
// irradiance from the entries' samples rather than their spherical harmonics
// defines IRCACHE_LOOKUP_PRECISE.

#ifndef IRCACHE_GLSL
#define IRCACHE_GLSL

// ── Sizes ────────────────────────────────────────────────────────────────

const uint IRCACHE_CASCADE_SIZE = 32u;
const uint IRCACHE_CASCADE_COUNT = 12u;
const float IRCACHE_CELL_DIAMETER = 0.16 * 0.125;   // of cascade 0, in metres
const uint IRCACHE_GRID_CELLS = IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_SIZE *
                                IRCACHE_CASCADE_COUNT;
const uint IRCACHE_MAX_ENTRIES = 65536u;

const uint IRCACHE_OCTA_DIMS = 4u;
const uint IRCACHE_OCTA_DIMS2 = IRCACHE_OCTA_DIMS * IRCACHE_OCTA_DIMS;
const uint IRCACHE_IRRADIANCE_STRIDE = 3u;                    // L1 spherical harmonics of r, g, b
const uint IRCACHE_AUX_STRIDE = 4u * IRCACHE_OCTA_DIMS2;     // reservoirs, values, trace origins, spare
const uint IRCACHE_SAMPLES_PER_FRAME = 4u;
const uint IRCACHE_VALIDATION_SAMPLES_PER_FRAME = 4u;
const float IRCACHE_RESTIR_M_CLAMP = 30.0;

// Cell flags, in the second uint of a cell.
const uint IRCACHE_OCCUPIED = 1u;
const uint IRCACHE_JUST_ALLOCATED = 2u;

// An entry's life counts frames up from four times the rank of its last
// lookup, and the entry is freed at 12. A lookup from a camera ray has rank
// 0, one from a rank-0 entry's rays rank 1, and so on.
const uint IRCACHE_LIFE_RECYCLE = 0x8000000u;
const uint IRCACHE_LIFE_RECYCLED = IRCACHE_LIFE_RECYCLE + 1u;
const uint IRCACHE_LIFE_PER_RANK = 4u;
const uint IRCACHE_RANK_COUNT = 3u;

// Slots of the meta buffer.
const uint META_TRACING_COUNT = 0u;   // live entries this frame, frozen before tracing
const uint META_INITIALIZED = 1u;
const uint META_ENTRY_COUNT = 2u;     // one past the highest entry index ever allocated
const uint META_ALLOC_COUNT = 3u;     // entries allocated from the pool
const uint META_LIVE_COUNT = 4u;      // entries listed in the indirection buffer

// Slots of the dispatch arguments buffer, each a uvec4.
const uint ARGS_AGE = 0u;
const uint ARGS_PER_ENTRY = 1u;
const uint ARGS_PER_SAMPLE = 2u;
const uint ARGS_PER_OCTA = 3u;

bool ircacheLifeValid(uint life) { return life < IRCACHE_LIFE_PER_RANK * IRCACHE_RANK_COUNT; }
uint ircacheLifeToRank(uint life) { return life / IRCACHE_LIFE_PER_RANK; }
uint ircacheLifeForRank(uint rank) { return rank * IRCACHE_LIFE_PER_RANK; }

// ── Buffers ──────────────────────────────────────────────────────────────

layout(set = IRCACHE_SET, binding = 0, std430) buffer IrcacheMeta { uint ircacheMeta[]; };
// Per cascade: [2c] its origin in cells, [2c + 1] how far it moved this
// frame; [2 * IRCACHE_CASCADE_COUNT] the eye position (float bits).
layout(set = IRCACHE_SET, binding = 1, std430) buffer IrcacheCascades { ivec4 ircacheCascades[]; };
// Per cell: [2i] its entry, [2i + 1] its flags.
layout(set = IRCACHE_SET, binding = 2, std430) buffer IrcacheGrid { uint ircacheGrid[]; };
layout(set = IRCACHE_SET, binding = 3, std430) buffer IrcacheGridScrolled { uint ircacheGridScrolled[]; };
layout(set = IRCACHE_SET, binding = 4, std430) buffer IrcacheEntryCell { uint ircacheEntryCell[]; };
// Per entry: the point and normal it traces from (packVertex).
layout(set = IRCACHE_SET, binding = 5, std430) buffer IrcacheSpatial { uvec4 ircacheSpatial[]; };
layout(set = IRCACHE_SET, binding = 6, std430) buffer IrcacheIrradiance { vec4 ircacheIrradiance[]; };
// Per entry, IRCACHE_AUX_STRIDE uvec4s: 16 reservoirs (xy), 16 values (rgb
// radiance and the reservoir's W, float bits), 16 trace origins (packVertex).
layout(set = IRCACHE_SET, binding = 7, std430) buffer IrcacheAux { uvec4 ircacheAux[]; };
layout(set = IRCACHE_SET, binding = 8, std430) buffer IrcacheLife { uint ircacheLife[]; };
layout(set = IRCACHE_SET, binding = 9, std430) buffer IrcachePool { uint ircachePool[]; };
layout(set = IRCACHE_SET, binding = 10, std430) buffer IrcacheProposal { uvec4 ircacheProposal[]; };
layout(set = IRCACHE_SET, binding = 11, std430) buffer IrcacheProposalCount { uint ircacheProposalCount[]; };
layout(set = IRCACHE_SET, binding = 12, std430) buffer IrcacheIndirection { uint ircacheIndirection[]; };
layout(set = IRCACHE_SET, binding = 13, std430) buffer IrcacheArgs { uvec4 ircacheArgs[]; };

// ── Hashing and packing ──────────────────────────────────────────────────

// Jenkins one-at-a-time.
uint hash1(uint x) {
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

uint hash1Mut(inout uint h) {
    uint result = h;
    h = hash1(h);
    return result;
}

float uintToU01(uint h) {
    return uintBitsToFloat((h & 0x007FFFFFu) | 0x3F800000u) - 1.0;
}

float randomU01(inout uint rng) {
    return uintToU01(hash1Mut(rng));
}

vec2 r2Sequence(uint i) {
    const float plastic = 1.32471795724474602596;
    return fract(vec2(1.0 / plastic, 1.0 / (plastic * plastic)) * float(i) + 0.5);
}

vec3 octaDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy -= (step(0.0, n.xy) * 2.0 - 1.0) * t;
    return normalize(n);
}

uint packNormal111011(vec3 n) {
    vec3 v = clamp(n * 0.5 + 0.5, 0.0, 1.0);
    return uint(round(v.x * 2047.0)) | (uint(round(v.y * 1023.0)) << 11) | (uint(round(v.z * 2047.0)) << 21);
}

vec3 unpackNormal111011(uint p) {
    return vec3(float(p & 2047u) * (2.0 / 2047.0) - 1.0,
                float((p >> 11) & 1023u) * (2.0 / 1023.0) - 1.0,
                float(p >> 21) * (2.0 / 2047.0) - 1.0);
}

struct IrcacheVertex {
    vec3 position;
    vec3 normal;
};

uvec4 packVertex(IrcacheVertex v) {
    return uvec4(floatBitsToUint(v.position), packNormal111011(v.normal));
}

IrcacheVertex unpackVertex(uvec4 p) {
    IrcacheVertex v;
    v.position = uintBitsToFloat(p.xyz);
    v.normal = unpackNormal111011(p.w);
    return v;
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 auxValue(uint index) { return uintBitsToFloat(ircacheAux[index].xyz); }
float auxWeight(uint index) { return uintBitsToFloat(ircacheAux[index].w); }
void setAuxValue(uint index, vec3 value, float weight) {
    ircacheAux[index] = uvec4(floatBitsToUint(value), floatBitsToUint(weight));
}

// ── The grid ─────────────────────────────────────────────────────────────

uint ircacheCellIndex(uvec3 coord, uint cascade) {
    return coord.x + IRCACHE_CASCADE_SIZE * (coord.y + IRCACHE_CASCADE_SIZE * (coord.z + IRCACHE_CASCADE_SIZE * cascade));
}

vec3 ircacheEye() {
    return intBitsToFloat(ircacheCascades[2u * IRCACHE_CASCADE_COUNT].xyz);
}

float ircacheCellDiameter(uint cascade) {
    return IRCACHE_CELL_DIAMETER * float(1u << cascade);
}

// The smallest cascade around the eye that holds a point `local` from it,
// keeping a cell to spare at its edge for the normal offset.
uint ircacheCascadeOf(vec3 local) {
    vec3 cells = local / IRCACHE_CELL_DIAMETER;
    float maxCoord = max(abs(cells.x), max(abs(cells.y), abs(cells.z)));
    float cascade = log2(maxCoord / float(IRCACHE_CASCADE_SIZE / 2u - 1u));
    return uint(clamp(ceil(max(0.0, cascade)), 0.0, float(IRCACHE_CASCADE_COUNT - 1u)));
}

struct IrcacheCoord {
    uvec3 coord;
    uint cascade;
};

// The cell a surface point looks up: in the cascade that holds it, half a
// cell along its normal. `jitter` (each component in [-0.5, 0.5], or zero)
// moves the point by up to half a cell, for stochastic interpolation.
IrcacheCoord ircacheCoordOf(vec3 position, vec3 normal, vec3 jitter) {
    vec3 eye = ircacheEye();
    position += ircacheCellDiameter(ircacheCascadeOf(position - eye)) * jitter;
    uint cascade = ircacheCascadeOf(position - eye);
    float diameter = ircacheCellDiameter(cascade);
    ivec3 origin = ircacheCascades[2u * cascade].xyz;
    ivec3 coord = ivec3(floor((position + normal * diameter * 0.5) / diameter)) - origin;
    IrcacheCoord result;
    result.cascade = cascade;
    result.coord = uvec3(clamp(coord, ivec3(0), ivec3(int(IRCACHE_CASCADE_SIZE) - 1)));
    return result;
}

// ── Samples ──────────────────────────────────────────────────────────────

// A sample of an entry: which of the 4 x 4 octahedral strata of the sphere
// it falls in, and a seed for its direction within it. Each frame an
// entry's samples take every fourth stratum, in a checkerboard, so four
// frames cover all sixteen.
uint sampleParams(uint samplesPerFrame, uint entry, uint sampleIndex, uint frame) {
    uint period = IRCACHE_OCTA_DIMS2 / samplesPerFrame;
    uint xy = sampleIndex * period + (frame % period);
    xy ^= (xy & 4u) >> 2u;
    return xy + ((frame << 16u) ^ entry) * IRCACHE_OCTA_DIMS2;
}

uint sampleOcta(uint params) { return params % IRCACHE_OCTA_DIMS2; }
uint sampleRng(uint params) { return hash1(params >> 4u); }

vec3 sampleDirection(uint params) {
    uint octa = sampleOcta(params);
    vec2 quant = vec2(octa % IRCACHE_OCTA_DIMS, octa / IRCACHE_OCTA_DIMS);
    return octaDecode((quant + r2Sequence(sampleRng(params) % 1024u)) / float(IRCACHE_OCTA_DIMS));
}

// ── Reservoirs ───────────────────────────────────────────────────────────

// One sample (`payload`) chosen from M candidates, with weight W. Stored
// as (payload, M and W as halves).
struct Reservoir {
    float wSum;
    uint payload;
    float M;
    float W;
};

struct ReservoirStream {
    float pSelected;
    float mSum;
};

Reservoir reservoirFromRaw(uvec2 raw) {
    vec2 mw = unpackHalf2x16(raw.y);
    return Reservoir(0.0, raw.x, mw.x, mw.y);
}

uvec2 reservoirRaw(Reservoir r) {
    return uvec2(r.payload, packHalf2x16(vec2(r.M, max(0.0, r.W))));
}

void reservoirInit(inout Reservoir r, float p, float weight, inout ReservoirStream stream, uint payload) {
    r.payload = payload;
    r.wSum = p * weight;
    r.M = weight != 0.0 ? 1.0 : 0.0;
    r.W = weight;
    stream.pSelected = p;
    stream.mSum = r.M;
}

bool reservoirUpdate(inout Reservoir r, float w, uint payload, inout uint rng) {
    r.wSum += w;
    r.M += 1.0;
    if (w / r.wSum >= randomU01(rng)) {
        r.payload = payload;
        return true;
    }
    return false;
}

// Merges reservoir `other`, whose sample has target density p.
bool reservoirMerge(inout Reservoir r, Reservoir other, float p, float weight, inout ReservoirStream stream,
                    uint payload, inout uint rng) {
    stream.mSum += other.M;
    if (reservoirUpdate(r, p * weight * other.W * other.M, payload, rng)) {
        stream.pSelected = p;
        return true;
    }
    return false;
}

void reservoirFinish(inout Reservoir r, ReservoirStream stream) {
    r.M = stream.mSum;
    r.W = r.wSum / max(1e-8, r.M * stream.pSelected);
}

// ── Lookup ───────────────────────────────────────────────────────────────

// L1 spherical harmonics evaluated for a normal, with Geomerics' non-linear
// reconstruction, which keeps the result positive.
float evalShGeomerics(vec4 sh, vec3 normal) {
    float r0 = sh.x;
    if (r0 <= 0.0) return 0.0;
    vec3 r1 = 0.5 * sh.yzw;
    float lenR1 = length(r1);
    if (lenR1 < 1e-8) return r0;
    float q = 0.5 * (1.0 + dot(r1 / lenR1, normal));
    float p = 1.0 + 2.0 * lenR1 / r0;
    float a = (1.0 - lenR1 / r0) / (1.0 + lenR1 / r0);
    return r0 * (a + (1.0 - a) * (p + 1.0) * pow(q, p));
}

struct IrcacheQuery {
    vec3 from;      // where the query comes from: the camera, or the entry that traced the ray
    vec3 position;  // the surface point
    vec3 normal;
    uint rank;
    bool jitter;    // stochastic interpolation between cells
};

// The irradiance (over pi) at a surface point from the cell it falls in.
// Allocates the cell's entry when it has none, keeps it alive at the
// query's rank, and votes for the point it should trace from next: a point
// a little towards where the query came from. An entry allocated this frame
// reads black.
vec3 ircacheLookup(IrcacheQuery q, inout uint rng) {
    vec3 jitter = q.jitter ? vec3(randomU01(rng), randomU01(rng), randomU01(rng)) - 0.5 : vec3(0.0);
    IrcacheCoord rc = ircacheCoordOf(q.position, q.normal, jitter);
    uint cell = ircacheCellIndex(rc.coord, rc.cascade);

    // A cell scrolled into its cascade this frame is allocated from camera
    // rays only, for a frame, since rays of other entries tend to leak.
    ivec3 scrolled = ircacheCascades[2u * rc.cascade + 1u].xyz;
    bvec3 scrolledIn = mix(lessThan(ivec3(rc.coord), -scrolled),
                           greaterThanEqual(ivec3(rc.coord) + scrolled, ivec3(IRCACHE_CASCADE_SIZE)),
                           greaterThan(scrolled, ivec3(0)));
    bool skipAllocation = q.rank >= IRCACHE_RANK_COUNT || (any(scrolledIn) && q.rank > 0u);

    uint flags = ircacheGrid[2u * cell + 1u];
    bool justAllocated = (flags & IRCACHE_JUST_ALLOCATED) != 0u;
    bool allocatedByUs = false;
    if (!skipAllocation && (flags & IRCACHE_OCCUPIED) == 0u) {
        uint previous = atomicOr(ircacheGrid[2u * cell + 1u], IRCACHE_OCCUPIED | IRCACHE_JUST_ALLOCATED);
        if ((previous & IRCACHE_OCCUPIED) == 0u) {
            justAllocated = true;
            allocatedByUs = true;
            uint allocation = atomicAdd(ircacheMeta[META_ALLOC_COUNT], 1u);
            if (allocation >= IRCACHE_MAX_ENTRIES) {
                atomicAdd(ircacheMeta[META_ALLOC_COUNT], uint(-1));
                atomicAnd(ircacheGrid[2u * cell + 1u], ~(IRCACHE_OCCUPIED | IRCACHE_JUST_ALLOCATED));
                allocatedByUs = false;
            } else {
                uint entry = ircachePool[allocation];
                atomicMax(ircacheMeta[META_ENTRY_COUNT], entry + 1u);
                ircacheLife[entry] = ircacheLifeForRank(q.rank);
                ircacheEntryCell[entry] = cell;
                ircacheGrid[2u * cell] = entry;
            }
        }
    }

    flags = ircacheGrid[2u * cell + 1u];
    if ((flags & IRCACHE_OCCUPIED) == 0u) return vec3(0.0);
    uint entry = ircacheGrid[2u * cell];

    float diameter = ircacheCellDiameter(rc.cascade);
    vec3 towardsQuery = q.from - q.position;
    towardsQuery *= diameter / max(diameter / 0.5, length(towardsQuery));
    IrcacheVertex proposal;
    proposal.position = q.position + towardsQuery;
    proposal.normal = q.normal;
    if (allocatedByUs) ircacheProposal[entry] = packVertex(proposal);
    if (justAllocated) return vec3(0.0);

    vec3 irradiance = vec3(0.0);
#ifdef IRCACHE_LOOKUP_PRECISE
    float weightSum = 0.0;
    for (uint octa = 0u; octa < IRCACHE_OCTA_DIMS2; ++octa) {
        Reservoir r = reservoirFromRaw(ircacheAux[entry * IRCACHE_AUX_STRIDE + octa].xy);
        float wt = dot(sampleDirection(r.payload), q.normal);
        if (wt > 0.0) {
            uint valueIndex = entry * IRCACHE_AUX_STRIDE + IRCACHE_OCTA_DIMS2 + octa;
            irradiance += auxValue(valueIndex) * wt * auxWeight(valueIndex);
            weightSum += wt;
        }
    }
    irradiance /= max(1.0, weightSum);
#else
    for (uint c = 0u; c < 3u; ++c) {
        irradiance[c] = evalShGeomerics(ircacheIrradiance[entry * IRCACHE_IRRADIANCE_STRIDE + c], q.normal);
    }
#endif
    irradiance = max(irradiance, vec3(0.0));

    // Keep the entry alive, at the lowest rank that uses it, and vote among
    // the queries of that rank for its next trace origin, each with the same
    // chance.
    uint previousLife = ircacheLife[entry];
    if (previousLife < IRCACHE_LIFE_RECYCLE) {
        uint life = ircacheLifeForRank(q.rank);
        if (life < previousLife) atomicMin(ircacheLife[entry], life);
        if (q.rank <= ircacheLifeToRank(previousLife)) {
            uint votes = atomicAdd(ircacheProposalCount[entry], 1u);
            if (randomU01(rng) <= 1.0 / (float(votes) + 1.0)) ircacheProposal[entry] = packVertex(proposal);
        }
    }
    return irradiance;
}

// The entry of the cell a surface point falls in, or ~0u: for the debug view.
uint ircacheEntryAt(vec3 position, vec3 normal) {
    IrcacheCoord rc = ircacheCoordOf(position, normal, vec3(0.0));
    uint cell = ircacheCellIndex(rc.coord, rc.cascade);
    return (ircacheGrid[2u * cell + 1u] & IRCACHE_OCCUPIED) != 0u ? ircacheGrid[2u * cell] : ~0u;
}

#endif
