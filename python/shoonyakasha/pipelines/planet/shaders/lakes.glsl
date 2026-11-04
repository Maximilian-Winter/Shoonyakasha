// Lakes on the planet (world.Lake). Within a lake's shore its ground is a
// bowl under its water level, `depth` deep at the centre and level with
// the water at the shore; beyond the shore it eases from the water level
// into the planet's own terrain over the lake's bank. The shore lies at the
// lake's radius, moved in and out by up to about its wobble times that by
// a noise of the offset from its centre, scaled to its radius. Distances
// are chords.
//
// Include after terrain.glsl, with these fields of TerrainParams in
// pipeline.json after siteShapes:
//     vec4 lakes[TERRAIN_LAKES];        // xyz: unit direction of its centre from the planet's centre;
//                                       // w: its water level (m)
//     vec4 lakeShapes[TERRAIN_LAKES];   // radius (m), depth at the centre (m), bank (m),
//                                       // wobble (share of the radius); a radius of 0 after the last

const int TERRAIN_LAKES = 32;
const float LAKE_WOBBLE_CYCLES = 1.5;    // the shore's noise per radius
const float LAKE_WOBBLE_REACH = 1.2;     // the noise's reach, as a share of the wobble, at most

// Metres from lake `i`'s centre to its shore towards `offset` (from its centre, m).
float lakeShore(int i, vec3 offset) {
    vec4 shape = terrain.lakeShapes[i];
    vec3 p = offset * (LAKE_WOBBLE_CYCLES / shape.x) + vec3(7.31 * float(i), 3.7, 1.3);
    return shape.x * (1.0 + shape.w * noise3(p));
}

// The offset (m) of unit direction `dir` from lake `i`'s centre.
vec3 lakeOffset(int i, vec3 dir) {
    return (dir - terrain.lakes[i].xyz) * terrain.siteShape.z;
}

// `height`, the planet's terrain at unit direction `dir`, with the lakes:
// the first lake whose shore holds `dir` gives its bowl; the banks of those
// whose bank holds it ease the height towards their water level.
float terrainWithLakes(float height, vec3 dir) {
    for (int i = 0; i < TERRAIN_LAKES; ++i) {
        vec4 shape = terrain.lakeShapes[i];
        if (shape.x <= 0.0) break;
        vec3 offset = lakeOffset(i, dir);
        float outer = shape.x * (1.0 + LAKE_WOBBLE_REACH * shape.w) + shape.z;
        float d2 = dot(offset, offset);
        if (d2 >= outer * outer) continue;
        float r = sqrt(d2);
        float shore = lakeShore(i, offset);
        float level = terrain.lakes[i].w;
        if (r < shore) {
            float t = r / shore;
            return level - shape.y * (1.0 - t * t);
        }
        height = mix(level, height, smoothstep(shore, shore + shape.z, r));
    }
    return height;
}

// The lake whose water covers unit direction `dir`, the first whose shore
// holds it: its index, or -1, and the water's depth there (m).
int lakeAt(vec3 dir, out float depth) {
    depth = 0.0;
    for (int i = 0; i < TERRAIN_LAKES; ++i) {
        vec4 shape = terrain.lakeShapes[i];
        if (shape.x <= 0.0) break;
        vec3 offset = lakeOffset(i, dir);
        float outer = shape.x * (1.0 + LAKE_WOBBLE_REACH * shape.w);
        float d2 = dot(offset, offset);
        if (d2 >= outer * outer) continue;
        float r = sqrt(d2);
        float shore = lakeShore(i, offset);
        if (r < shore) {
            float t = r / shore;
            depth = shape.y * (1.0 - t * t);
            return i;
        }
    }
    return -1;
}

// The lake whose water comes nearest unit direction `dir`, within `reach`
// metres of it: its index, or -1.
int lakeNear(vec3 dir, float reach) {
    int nearest = -1;
    float gap = reach;
    for (int i = 0; i < TERRAIN_LAKES; ++i) {
        vec4 shape = terrain.lakeShapes[i];
        if (shape.x <= 0.0) break;
        float g = length(lakeOffset(i, dir)) - shape.x * (1.0 + LAKE_WOBBLE_REACH * shape.w);
        if (g < gap) {
            gap = g;
            nearest = i;
        }
    }
    return nearest;
}
