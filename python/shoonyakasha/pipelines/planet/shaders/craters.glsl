// Craters on either body (world.Crater): a bowl `depth` metres deep at
// the centre, reaching the ground at the crater's radius, with a rim
// `rim` metres high around it; the rim fades out by twice the radius.
// The shape is added to the body's ground. In units of the radius r, it
// is the smooth minimum (over 0.3 times the depth) of the bowl,
// depth * (r^2 - 1), and the rim, rim * exp(-((r - 1) / 0.25)^2), times
// 1 - smoothstep(1.2, 2, r). Distances are chords.
//
// Include after declaring the uniform block `terrain` with these fields of
// TerrainParams in pipeline.json after riverOrigin:
//     vec4 craters[TERRAIN_CRATERS];       // xyz: unit direction from the body's centre; w: radius (m)
//     vec4 craterShapes[TERRAIN_CRATERS];  // depth (m), rim height (m), body index, the body's radius (m);
//                                          // a radius of 0 leaves the entry unused

const int TERRAIN_CRATERS = 16;
const float CRATER_REACH = 2.0;          // radii from the centre where a crater's shape ends
const float CRATER_RIM_WIDTH = 0.25;     // radii
const float CRATER_SMOOTHNESS = 0.3;     // of the depth

float craterSmoothMin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// A crater's shape (m) at `r` radii from its centre.
float craterShape(float r, float depth, float rim) {
    if (r >= CRATER_REACH) return 0.0;
    float bowl = depth * (r * r - 1.0);
    float x = (r - 1.0) / CRATER_RIM_WIDTH;
    float ridge = rim * exp(-x * x);
    return craterSmoothMin(bowl, ridge, max(CRATER_SMOOTHNESS * depth, 1e-3))
         * (1.0 - smoothstep(1.2, CRATER_REACH, r));
}

// `height`, the ground of body `body` at unit direction `dir`, with the
// craters on that body whose radius is at least `smallest` metres.
float terrainWithCraters(float height, vec3 dir, int body, float smallest) {
    for (int i = 0; i < TERRAIN_CRATERS; ++i) {
        vec4 crater = terrain.craters[i];
        vec4 shape = terrain.craterShapes[i];
        if (crater.w <= 0.0 || crater.w < smallest || int(shape.z) != body) continue;
        float distance = length(dir - crater.xyz) * shape.w;
        height += craterShape(distance / crater.w, shape.x, shape.y);
    }
    return height;
}

// Whether unit direction `dir` on body `body` lies within a crater's radius.
bool terrainInCrater(vec3 dir, int body) {
    for (int i = 0; i < TERRAIN_CRATERS; ++i) {
        vec4 crater = terrain.craters[i];
        vec4 shape = terrain.craterShapes[i];
        if (crater.w <= 0.0 || int(shape.z) != body) continue;
        if (length(dir - crater.xyz) * shape.w < crater.w) return true;
    }
    return false;
}
