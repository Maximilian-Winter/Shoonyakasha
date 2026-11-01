// Ore richness, 0 .. 1, on the planet and the moon. Include after
// terrain.glsl (and moon.glsl for moonOre).
//
// planetOre(dir, height): veins, ridged noise about ORE_VEIN_WAVELENGTH
// across (the ridges' share above ORE_VEIN_LOW, rising to 1 at
// ORE_VEIN_HIGH), times patches, fBm about ORE_PATCH_WAVELENGTH across
// (rising from ORE_PATCH_LOW to ORE_PATCH_HIGH), times 0.4 + 0.6 of the
// mountains' mask; none below sea level.
//
// moonOre(dir): the craters of the largest MOON_ORE_OCTAVES sizes (moon.glsl's
// lattice and hashes) each have a mineral value, a hashed unit number; a
// crater whose value is above MOON_ORE_LOW holds ore of grade rising from 0
// there to 1 at 1, over its floor and its rim, fading out between
// MOON_ORE_FADE and the rim's outer edge. Where craters overlap the richest
// counts.

const float ORE_VEIN_WAVELENGTH = 0.0045;    // of a unit of direction: about 450 m on the planet
const float ORE_PATCH_WAVELENGTH = 0.0167;   // about 1.7 km
const float ORE_VEIN_LOW = 0.45;
const float ORE_VEIN_HIGH = 0.85;
const float ORE_PATCH_LOW = 0.0;
const float ORE_PATCH_HIGH = 0.3;
const int MOON_ORE_OCTAVES = 3;
const float MOON_ORE_LOW = 0.6;
const float MOON_ORE_FADE = 1.3;             // of a crater's radius

float planetOre(vec3 dir, float height) {
    if (height <= 0.0) return 0.0;
    vec3 p = dir + terrain.seed.xyz;
    float veins = smoothstep(ORE_VEIN_LOW, ORE_VEIN_HIGH,
                             ridged3(p / ORE_VEIN_WAVELENGTH + vec3(3.1, 8.7, 1.9), 4, 2.1, 0.5));
    if (veins <= 0.0) return 0.0;
    float patches = smoothstep(ORE_PATCH_LOW, ORE_PATCH_HIGH,
                               fbm3(p / ORE_PATCH_WAVELENGTH + vec3(6.2, 1.4, 4.8), 4, 2.0, 0.5));
    if (patches <= 0.0) return 0.0;
    return clamp(veins * patches * (0.4 + 0.6 * terrainMountainMask(dir)), 0.0, 1.0);
}

float moonOre(vec3 dir) {
    float richness = 0.0;
    float cells = MOON_CRATER_CELLS;
    for (int octave = 0; octave < MOON_ORE_OCTAVES; ++octave) {
        vec3 p = dir * cells;
        ivec3 first = ivec3(floor(p - 0.5));
        for (int k = 0; k < 8; ++k) {
            ivec3 c = first + ivec3(k & 1, (k >> 1) & 1, k >> 2);
            uint h = terrainHash(c + ivec3(octave * 7919, octave * 104729, 0));
            if (moonUnit(h) >= MOON_CRATER_CHANCE) continue;
            float mineral = moonUnit(terrainHash(ivec3(int(h), 5, 0)));
            if (mineral <= MOON_ORE_LOW) continue;
            vec3 centre = vec3(c) + vec3(moonUnit(terrainHash(ivec3(int(h), 1, 0))),
                                         moonUnit(terrainHash(ivec3(int(h), 2, 0))),
                                         moonUnit(terrainHash(ivec3(int(h), 3, 0))));
            float r = length(centre);
            if (abs(r - cells) > 0.5) continue;
            centre *= cells / r;
            float size = mix(MOON_CRATER_SIZE.x, MOON_CRATER_SIZE.y, moonUnit(terrainHash(ivec3(int(h), 4, 0))));
            float x = length(p - centre) / size;
            float weight = 1.0 - smoothstep(MOON_ORE_FADE, 1.0 + MOON_RIM_WIDTH, x);
            richness = max(richness, (mineral - MOON_ORE_LOW) / (1.0 - MOON_ORE_LOW) * weight);
        }
        cells *= MOON_CRATER_LACUNARITY;
    }
    return richness;
}
