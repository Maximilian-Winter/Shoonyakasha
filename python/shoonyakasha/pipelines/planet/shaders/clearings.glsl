// Clearings on either body (world.Clearing): circles and rectangles where
// the scatter leaves out the kinds a clearing clears (CLEAR_* bits), with
// an edge outside the border over which they thin in to their full
// density. The terrain is left as it is. Distances are chords.
//
// Include after declaring the uniform block `vegetation` with these fields
// of VegetationParams in pipeline.json:
//     vec4 clearings[VEGETATION_CLEARINGS];       // xyz: unit direction of its centre from its body's centre;
//                                                 // w: its body's index
//     vec4 clearingShapes[VEGETATION_CLEARINGS];  // x: a circle's radius or a rectangle's half length (m);
//                                                 // y: 0 for a circle, a rectangle's half width (m);
//                                                 // z: its edge (m); w: the kinds it clears, 0 after the last
//     vec4 clearingAxes[VEGETATION_CLEARINGS];    // xyz: unit direction of a rectangle's length;
//                                                 // w: its body's radius (m)

const int VEGETATION_CLEARINGS = 32;
const uint CLEAR_PLANTS = 1u;     // trees, bushes, shrubs and cacti
const uint CLEAR_GRASS = 2u;
const uint CLEAR_ROCKS = 4u;      // small rocks and boulders

// Metres from unit direction `dir` to clearing `i`'s border, negative inside.
float clearingDistance(int i, vec3 dir) {
    vec3 centre = vegetation.clearings[i].xyz;
    vec4 shape = vegetation.clearingShapes[i];
    vec4 axis = vegetation.clearingAxes[i];
    vec3 offset = (dir - centre) * axis.w;
    if (shape.y <= 0.0) return length(offset) - shape.x;
    vec2 q = abs(vec2(dot(offset, axis.xyz), dot(offset, cross(centre, axis.xyz)))) - shape.xy;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0);
}

// The share of the scatter of `kinds` (CLEAR_* bits) kept at unit direction
// `dir` on body `body`: 0 within a clearing that clears any of them, rising
// linearly to 1 across its edge, 1 elsewhere; the least over the clearings.
float clearingKeep(vec3 dir, int body, uint kinds) {
    float keep = 1.0;
    for (int i = 0; i < VEGETATION_CLEARINGS; ++i) {
        vec4 shape = vegetation.clearingShapes[i];
        uint clears = uint(shape.w);
        if (clears == 0u) break;
        if ((clears & kinds) == 0u || int(vegetation.clearings[i].w) != body) continue;
        float d = clearingDistance(i, dir);
        keep = min(keep, shape.z > 0.0 ? clamp(d / shape.z, 0.0, 1.0) : (d > 0.0 ? 1.0 : 0.0));
        if (keep <= 0.0) break;
    }
    return keep;
}
