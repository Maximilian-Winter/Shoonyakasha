// Rivers on the planet (world.River, shoonyakasha.planet.rivers): segments
// from one point of a river's centre line to the next, each with the water
// level, width and depth at its ends, looked up by a grid of cells in the
// plane of a frame (the height grid's, rivers.GridFrame). Within half a
// segment's width of its centre line the ground is a channel under the
// water level, `depth` deep at the centre line and level with the water at
// its edge; beyond the edge it eases from the water level into the
// planet's own terrain over a bank RIVER_BANK wide plus RIVER_BANK_SLOPE
// metres per metre the terrain stands above the water, at most
// RIVER_BANK_MAX. The ground is the lowest of the terrain and what each
// segment of its cell gives, so a river never raises it. Distances are
// chords.
//
// Include after terrain.glsl, with these fields of TerrainParams in
// pipeline.json after lakeShapes:
//     vec4 riverFrame[4];   // [0] xyz: the frame's centre (a unit direction), w: segments (0: none);
//                           // [1] xyz: its east, w: a cell's size (m); [2] xyz: its north, w: cells per side;
//                           // [3] x: the first vec4 of the indices, y: the first of the segments
// and a readonly storage buffer of vec4 named `riverData.data`: first a
// (first index, count) per cell, row by row along east; then the indices
// of each cell's segments, four to a vec4; then three vec4 per segment:
// (one end's unit direction, its level), (the other end's, its level),
// (their widths, their depths), in metres.

const float RIVER_BANK = 8.0;
const float RIVER_BANK_SLOPE = 2.5;
const float RIVER_BANK_MAX = 120.0;

// The cell of the lookup grid holding unit direction `dir`: its first
// index and count, or false outside the grid.
bool riverCell(vec3 dir, out int first, out int count) {
    first = 0;
    count = 0;
    vec4 c = terrain.riverFrame[0];
    if (c.w <= 0.0) return false;
    float along = dot(dir, c.xyz);
    if (along <= 0.5) return false;
    float radius = terrain.siteShape.z;
    vec3 p = dir * (radius / along) - c.xyz * radius;
    float cell = terrain.riverFrame[1].w, cells = terrain.riverFrame[2].w;
    float extent = 0.5 * cells * cell;
    int i = int(floor((dot(p, terrain.riverFrame[1].xyz) + extent) / cell));
    int j = int(floor((dot(p, terrain.riverFrame[2].xyz) + extent) / cell));
    if (i < 0 || j < 0 || i >= int(cells) || j >= int(cells)) return false;
    vec4 entry = riverData.data[j * int(cells) + i];
    first = int(entry.x);
    count = int(entry.y);
    return count > 0;
}

// The `k`th segment index from `first` in the indices.
int riverIndex(int first, int k) {
    int at = first + k;
    return int(riverData.data[int(terrain.riverFrame[3].x) + at / 4][at % 4]);
}

// Segment `s` at unit direction `dir`: metres from its centre line, the
// level, half width and depth across from `dir`.
float riverSegment(int s, vec3 dir, out float level, out float halfWidth, out float depth) {
    int row = int(terrain.riverFrame[3].y) + 3 * s;
    vec4 a = riverData.data[row], b = riverData.data[row + 1], shape = riverData.data[row + 2];
    float radius = terrain.siteShape.z;
    vec3 pa = (dir - a.xyz) * radius, ba = (b.xyz - a.xyz) * radius;
    float t = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    level = mix(a.w, b.w, t);
    halfWidth = 0.5 * mix(shape.x, shape.y, t);
    depth = mix(shape.z, shape.w, t);
    return length(pa - ba * t);
}

// `height`, the planet's terrain at unit direction `dir`, with the rivers.
float terrainWithRivers(float height, vec3 dir) {
    int first, count;
    if (!riverCell(dir, first, count)) return height;
    float lowest = height;
    for (int k = 0; k < count; ++k) {
        float level, halfWidth, depth;
        float r = riverSegment(riverIndex(first, k), dir, level, halfWidth, depth);
        float h;
        if (r < halfWidth) {
            float u = r / halfWidth;
            h = level - depth * (1.0 - u * u);
        } else {
            float bank = min(RIVER_BANK + RIVER_BANK_SLOPE * max(height - level, 0.0), RIVER_BANK_MAX);
            h = mix(level, height, smoothstep(halfWidth, halfWidth + bank, r));
        }
        lowest = min(lowest, h);
    }
    return lowest;
}

// Whether a river's water comes within `reach` metres of unit direction
// `dir`: a segment of a lookup cell within `reach` of it whose water's edge
// passes that near.
bool riverNear(vec3 dir, float reach) {
    vec4 c = terrain.riverFrame[0];
    if (c.w <= 0.0) return false;
    float along = dot(dir, c.xyz);
    if (along <= 0.5) return false;
    float radius = terrain.siteShape.z;
    vec3 p = dir * (radius / along) - c.xyz * radius;
    float cell = terrain.riverFrame[1].w, cells = terrain.riverFrame[2].w;
    float extent = 0.5 * cells * cell;
    float x = dot(p, terrain.riverFrame[1].xyz) + extent, y = dot(p, terrain.riverFrame[2].xyz) + extent;
    int last = int(cells) - 1;
    int i0 = max(int(floor((x - reach) / cell)), 0), i1 = min(int(floor((x + reach) / cell)), last);
    int j0 = max(int(floor((y - reach) / cell)), 0), j1 = min(int(floor((y + reach) / cell)), last);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            vec4 entry = riverData.data[j * int(cells) + i];
            for (int k = 0; k < int(entry.y); ++k) {
                float level, halfWidth, depth;
                if (riverSegment(riverIndex(int(entry.x), k), dir, level, halfWidth, depth) < halfWidth + reach) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Whether a river's water covers unit direction `dir`, with the water's
// depth there (m): the deepest of the segments whose water covers it.
bool riverAt(vec3 dir, out float depth) {
    depth = 0.0;
    int first, count;
    if (!riverCell(dir, first, count)) return false;
    bool wet = false;
    for (int k = 0; k < count; ++k) {
        float level, halfWidth, d;
        float r = riverSegment(riverIndex(first, k), dir, level, halfWidth, d);
        if (r < halfWidth) {
            float u = r / halfWidth;
            depth = max(depth, d * (1.0 - u * u));
            wet = true;
        }
    }
    return wet;
}
