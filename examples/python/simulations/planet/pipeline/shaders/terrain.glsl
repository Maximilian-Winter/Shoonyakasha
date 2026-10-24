// The planet's terrain: terrainHeight(dir), metres above the sphere of
// radius `radius` at the unit direction `dir` from the planet's centre.
//
// Include after declaring a uniform block named `terrain` with the fields
// of TerrainParams in pipeline.json:
//     vec4 continent;   // frequency, warp strength, sea level bias, height scale (m)
//     vec4 mountains;   // frequency, height (m), mask frequency, octaves
//     vec4 hills;       // frequency, height (m), ocean depth (m), octaves
//     vec4 seed;        // xyz: offset added to the noise input
//     vec4 site;        // xyz: unit direction of a flat pad; w: its height (m)
//     vec4 siteShape;   // flat radius (m), radius where the terrain is its own again (m),
//                       // planet radius (m), 1 with the pad or 0 without
//
// The noise hashes integer lattice coordinates, so the same `dir` gives the
// same height in every chunk and every pass.

// Integer hash of a lattice point.
uint terrainHash(ivec3 c) {
    uvec3 q = uvec3(c);
    uint h = (q.x * 1597334677u) ^ (q.y * 3812015801u) ^ (q.z * 2741598923u);
    h = h * 747796405u + 2891336453u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return h;
}

// The twelve gradient directions: the edges of a cube.
const vec3 TERRAIN_GRADIENTS[12] = vec3[12](
    vec3(1, 1, 0), vec3(-1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0),
    vec3(1, 0, 1), vec3(-1, 0, 1), vec3(1, 0, -1), vec3(-1, 0, -1),
    vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, 1, -1), vec3(0, -1, -1));

vec3 terrainGradient(ivec3 c) {
    return TERRAIN_GRADIENTS[terrainHash(c) % 12u];
}

// Gradient noise in about [-1, 1], with quintic interpolation.
float noise3(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = p - cell;
    ivec3 c = ivec3(cell);
    vec3 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float n000 = dot(terrainGradient(c + ivec3(0, 0, 0)), f - vec3(0, 0, 0));
    float n100 = dot(terrainGradient(c + ivec3(1, 0, 0)), f - vec3(1, 0, 0));
    float n010 = dot(terrainGradient(c + ivec3(0, 1, 0)), f - vec3(0, 1, 0));
    float n110 = dot(terrainGradient(c + ivec3(1, 1, 0)), f - vec3(1, 1, 0));
    float n001 = dot(terrainGradient(c + ivec3(0, 0, 1)), f - vec3(0, 0, 1));
    float n101 = dot(terrainGradient(c + ivec3(1, 0, 1)), f - vec3(1, 0, 1));
    float n011 = dot(terrainGradient(c + ivec3(0, 1, 1)), f - vec3(0, 1, 1));
    float n111 = dot(terrainGradient(c + ivec3(1, 1, 1)), f - vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, w.x), mix(n010, n110, w.x), w.y),
               mix(mix(n001, n101, w.x), mix(n011, n111, w.x), w.y), w.z);
}

float fbm3(vec3 p, int octaves, float lacunarity, float gain) {
    float amplitude = 0.5, sum = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * noise3(p);
        p = p * lacunarity + vec3(19.1, 7.3, 3.7);
        amplitude *= gain;
    }
    return sum;
}

// Ridges where the noise crosses zero; each octave weighted by the one
// before, so ridges get sharper detail than valleys.
float ridged3(vec3 p, int octaves, float lacunarity, float gain) {
    float amplitude = 0.5, sum = 0.0, weight = 1.0;
    for (int i = 0; i < octaves; ++i) {
        float n = 1.0 - abs(noise3(p));
        n *= n;
        n *= weight;
        weight = clamp(n * 2.0, 0.0, 1.0);
        sum += amplitude * n;
        p = p * lacunarity + vec3(5.3, 11.9, 2.1);
        amplitude *= gain;
    }
    return sum;
}

vec3 warp3(vec3 p, float strength) {
    return p + strength * vec3(noise3(p + vec3(1.7, 9.2, 3.1)),
                               noise3(p + vec3(8.3, 2.8, 5.5)),
                               noise3(p + vec3(4.1, 6.6, 7.9)));
}

// Continent value at `dir`: below 0 is ocean, above is land.
float terrainContinent(vec3 dir) {
    vec3 p = dir + terrain.seed.xyz;
    return fbm3(warp3(p * terrain.continent.x, terrain.continent.y), 5, 2.0, 0.5) + terrain.continent.z;
}

// The terrain without the pad.
float terrainHeightNatural(vec3 dir) {
    vec3 p = dir + terrain.seed.xyz;
    float c = terrainContinent(dir);
    float land = smoothstep(-0.05, 0.05, c);

    float mask = smoothstep(0.1, 0.4, fbm3(p * terrain.mountains.z, 3, 2.0, 0.5) + 0.25);
    float mountains = ridged3(p * terrain.mountains.x, int(terrain.mountains.w), 2.1, 0.5) * mask;
    float hills = fbm3(p * terrain.hills.x, int(terrain.hills.w), 2.0, 0.5);

    float onLand = c * terrain.continent.w + mountains * terrain.mountains.y + hills * terrain.hills.y;
    float seabed = -terrain.hills.z * smoothstep(0.0, -0.3, c);
    return mix(seabed, onLand, land);
}

// The terrain with the pad: the pad's height within its flat radius, easing
// into the natural terrain by the outer radius. Distances are chords, which
// keep their precision in 32-bit floats where an angle from acos would not.
float terrainHeight(vec3 dir) {
    float natural = terrainHeightNatural(dir);
    if (terrain.siteShape.w == 0.0) return natural;
    float distance = length(dir - terrain.site.xyz) * terrain.siteShape.z;
    return mix(terrain.site.w, natural, smoothstep(terrain.siteShape.x, terrain.siteShape.y, distance));
}
