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
//     vec4 detail;      // wavelength (m), height (m), octaves, erosion of the hills and the detail
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

// noise3 (x) and its gradient (yzw).
vec4 noised3(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = p - cell;
    ivec3 c = ivec3(cell);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec3 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    vec3 ga = terrainGradient(c + ivec3(0, 0, 0)), gb = terrainGradient(c + ivec3(1, 0, 0));
    vec3 gc = terrainGradient(c + ivec3(0, 1, 0)), gd = terrainGradient(c + ivec3(1, 1, 0));
    vec3 ge = terrainGradient(c + ivec3(0, 0, 1)), gf = terrainGradient(c + ivec3(1, 0, 1));
    vec3 gg = terrainGradient(c + ivec3(0, 1, 1)), gh = terrainGradient(c + ivec3(1, 1, 1));
    float va = dot(ga, f - vec3(0, 0, 0)), vb = dot(gb, f - vec3(1, 0, 0));
    float vc = dot(gc, f - vec3(0, 1, 0)), vd = dot(gd, f - vec3(1, 1, 0));
    float ve = dot(ge, f - vec3(0, 0, 1)), vf = dot(gf, f - vec3(1, 0, 1));
    float vg = dot(gg, f - vec3(0, 1, 1)), vh = dot(gh, f - vec3(1, 1, 1));
    float k1 = vb - va, k2 = vc - va, k3 = ve - va;
    float k4 = va - vb - vc + vd, k5 = va - vc - ve + vg, k6 = va - vb - ve + vf;
    float k7 = -va + vb + vc - vd + ve - vf - vg + vh;
    float value = va + u.x * k1 + u.y * k2 + u.z * k3 + u.x * u.y * k4 + u.y * u.z * k5 + u.z * u.x * k6
                + u.x * u.y * u.z * k7;
    vec3 gradient = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga)
                  + u.x * u.y * (ga - gb - gc + gd) + u.y * u.z * (ga - gc - ge + gg)
                  + u.z * u.x * (ga - gb - ge + gf) + u.x * u.y * u.z * (-ga + gb + gc - gd + ge - gf - gg + gh)
                  + du * vec3(k1 + u.y * k4 + u.z * k6 + u.y * u.z * k7,
                              k2 + u.z * k5 + u.x * k4 + u.z * u.x * k7,
                              k3 + u.x * k6 + u.y * k5 + u.x * u.y * k7);
    return vec4(value, gradient);
}

// fBm whose octaves are damped where the octaves before have built up
// slope: each adds amplitude * n / (1 + erosion * |sum of gradients|^2), so
// slopes stay smooth and detail gathers on flats and crests. With erosion 0
// it is fbm3.
float erodedFbm3(vec3 p, int octaves, float lacunarity, float gain, float erosion) {
    float amplitude = 0.5, sum = 0.0;
    vec3 slope = vec3(0.0);
    for (int i = 0; i < octaves; ++i) {
        vec4 n = noised3(p);
        slope += n.yzw;
        sum += amplitude * n.x / (1.0 + erosion * dot(slope, slope));
        p = p * lacunarity + vec3(19.1, 7.3, 3.7);
        amplitude *= gain;
    }
    return sum;
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
    float erosion = terrain.detail.w;
    float hills = erodedFbm3(p * terrain.hills.x, int(terrain.hills.w), 2.0, 0.5, erosion);
    // Detail below the hills' octaves, in metres of wavelength; its own small offset
    // from the seed keeps the noise input small enough for 32-bit floats.
    vec3 detailInput = dir * (terrain.siteShape.z / terrain.detail.x) + fract(terrain.seed.xyz * 0.0731) * 16.0;
    float detail = erodedFbm3(detailInput, int(terrain.detail.z), 2.0, 0.5, erosion) * (0.35 + 0.65 * mask);

    float onLand = c * terrain.continent.w + mountains * terrain.mountains.y + hills * terrain.hills.y
                 + detail * terrain.detail.y;
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
