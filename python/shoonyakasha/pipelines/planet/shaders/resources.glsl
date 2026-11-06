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
//
// Resources (the RESOURCE_ ids, which shoonyakasha.planet.resources
// mirrors), for where the richness is above 0:
//
// planetResource(dir, temperature, moisture): of ferrite, cuprite, quartz,
// amber and evaporite, the one whose weight (planetResourceWeights, from
// the base climate and the mountains' mask) times
// 1 + RESOURCE_NOISE times its own fBm about RESOURCE_WAVELENGTH across
// (planetResourceNoise) is the highest; ferrite where they tie.
//
// moonResource(dir): ice where the richest ore crater's centre lies beyond
// MOON_ICE_LATITUDE (the sine of its latitude, about the Y axis), ilmenite
// elsewhere.

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

// The moon's richness at `dir`, and the unit direction of the crater it comes from (dir where there is none).
float moonOreCrater(vec3 dir, out vec3 crater) {
    crater = dir;
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
            float grade = (mineral - MOON_ORE_LOW) / (1.0 - MOON_ORE_LOW) * weight;
            if (grade > richness) {
                richness = grade;
                crater = centre / cells;
            }
        }
        cells *= MOON_CRATER_LACUNARITY;
    }
    return richness;
}

float moonOre(vec3 dir) {
    vec3 crater;
    return moonOreCrater(dir, crater);
}

const uint RESOURCE_NONE = 0u;
const uint RESOURCE_FERRITE = 1u;
const uint RESOURCE_CUPRITE = 2u;
const uint RESOURCE_QUARTZ = 3u;
const uint RESOURCE_AMBER = 4u;
const uint RESOURCE_EVAPORITE = 5u;
const uint RESOURCE_ICE = 6u;
const uint RESOURCE_ILMENITE = 7u;
const uint RESOURCE_IRIDIUM = 8u;            // meteorites only: never chosen here
const int PLANET_RESOURCES = 5;              // ferrite to evaporite
const float RESOURCE_WAVELENGTH = 0.11;      // of a unit of direction: about 11 km on the planet
const float RESOURCE_NOISE = 0.6;
const float MOON_ICE_LATITUDE = 0.55;

// 1 below `low`, falling to 0 at `high`.
float resourceFalling(float low, float high, float x) {
    return 1.0 - smoothstep(low, high, x);
}

// The planet's resources' weights, ferrite first, from the base temperature
// (degrees C), the moisture and the mountains' mask.
void planetResourceWeights(float temperature, float moisture, float mountains, out float w[PLANET_RESOURCES]) {
    w[0] = 0.55 * smoothstep(0.3, 0.9, mountains);                                        // ferrite
    w[1] = resourceFalling(0.3, 0.5, moisture) * smoothstep(3.0, 12.0, temperature)
         * resourceFalling(16.0, 24.0, temperature);                                      // cuprite
    w[2] = resourceFalling(-4.0, 2.0, temperature);                                       // quartz
    w[3] = smoothstep(0.5, 0.75, moisture) * smoothstep(10.0, 20.0, temperature);         // amber
    w[4] = resourceFalling(0.25, 0.4, moisture) * smoothstep(12.0, 20.0, temperature);    // evaporite
}

// The fBm that varies resource `k` (0 ferrite .. 4 evaporite) over the planet.
float planetResourceNoise(vec3 dir, int k) {
    vec3 p = (dir + terrain.seed.xyz) / RESOURCE_WAVELENGTH + vec3(float(k) * 17.3, float(k) * 5.9, 2.7);
    return fbm3(p, 3, 2.0, 0.5);
}

// The resource of the highest weight times 1 + RESOURCE_NOISE times its noise; ferrite where they tie.
uint planetResourcePick(float w[PLANET_RESOURCES], float noise[PLANET_RESOURCES]) {
    uint best = RESOURCE_FERRITE;
    float bestWeight = w[0] * (1.0 + RESOURCE_NOISE * noise[0]);
    for (int k = 1; k < PLANET_RESOURCES; ++k) {
        float weight = w[k] * (1.0 + RESOURCE_NOISE * noise[k]);
        if (weight > bestWeight) {
            bestWeight = weight;
            best = RESOURCE_FERRITE + uint(k);
        }
    }
    return best;
}

uint planetResource(vec3 dir, float temperature, float moisture) {
    float w[PLANET_RESOURCES];
    planetResourceWeights(temperature, moisture, terrainMountainMask(dir), w);
    float noise[PLANET_RESOURCES];
    for (int k = 0; k < PLANET_RESOURCES; ++k) noise[k] = planetResourceNoise(dir, k);
    return planetResourcePick(w, noise);
}

uint moonResource(vec3 dir) {
    vec3 crater;
    moonOreCrater(dir, crater);
    return abs(crater.y) > MOON_ICE_LATITUDE ? RESOURCE_ICE : RESOURCE_ILMENITE;
}
