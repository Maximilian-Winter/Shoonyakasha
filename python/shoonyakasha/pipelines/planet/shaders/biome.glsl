// The planet's biomes and its ground as layers. A point's climate
// (climate.glsl's climateAt, from the chunk's vertices) plus the climate
// settings' offsets gives each biome a weight (biomeWeights); the biomes
// give the ground's layers their weights and colours (groundLayers), and
// the vegetation its chances, kinds and colours (biomePlants). The
// terrain's colours (terrain.frag, ground.glsl) and the vegetation
// (vegetation_scatter.comp, grass_scatter.comp) all read them. Include
// after terrain.glsl (for `terrain` with its `climate` and `biomeView`).
//
//     terrain.climate    x: degrees C added to the temperature; y: added to
//                        the moisture; z: the flora's hue turn (radians);
//                        w: the soil's hue turn (radians)
//     terrain.biomeView  x: 1 colours the ground by its biomes (BIOME_VIEW)
//
// Biomes, by temperature band (rows) and moisture band (columns):
//
//                   < 0.15     0.15-0.32   0.32-0.55   > 0.55
//     below 0 C     tundra     tundra      tundra      tundra
//     0 to 7 C      steppe     steppe      taiga       taiga
//     7 to 19 C     desert     steppe      grassland   deciduous
//     above 19 C    desert     desert      savanna     tropical
//
// Each band edge is a smoothstep BIOME_T_BLEND degrees or BIOME_M_BLEND of
// moisture to either side, so the weights sum to 1 and change smoothly.
// Snow lies where the temperature, warmer or colder by the slope's aspect
// (geology.glsl's aspectWarmth), is below SNOW_TEMPERATURE, on slopes less
// steep than a cliff; sand on beaches below 40 m; rock on steep slopes;
// scree (geology.glsl) on the open ground under cliffs. A climate's cover
// (z) is read through coverOfClimate, its scree through screeOfClimate.

#include "geology.glsl"

const int BIOME_DESERT = 0;
const int BIOME_SAVANNA = 1;
const int BIOME_TROPICAL = 2;
const int BIOME_STEPPE = 3;
const int BIOME_GRASSLAND = 4;
const int BIOME_DECIDUOUS = 5;
const int BIOME_TAIGA = 6;
const int BIOME_TUNDRA = 7;
const int BIOMES = 8;

const vec3 BIOME_T_EDGES = vec3(0.0, 7.0, 19.0);
const vec3 BIOME_M_EDGES = vec3(0.15, 0.32, 0.55);
const float BIOME_T_BLEND = 1.5;
const float BIOME_M_BLEND = 0.04;
const float SNOW_TEMPERATURE = -6.0;

const int BIOME_TABLE[16] = int[16](
    BIOME_TUNDRA, BIOME_TUNDRA, BIOME_TUNDRA, BIOME_TUNDRA,
    BIOME_STEPPE, BIOME_STEPPE, BIOME_TAIGA, BIOME_TAIGA,
    BIOME_DESERT, BIOME_STEPPE, BIOME_GRASSLAND, BIOME_DECIDUOUS,
    BIOME_DESERT, BIOME_DESERT, BIOME_SAVANNA, BIOME_TROPICAL);

const int GROUND_SAND = 0;
const int GROUND_DRY = 1;
const int GROUND_GRASS = 2;
const int GROUND_FOREST = 3;
const int GROUND_ROCK = 4;
const int GROUND_SNOW = 5;
const int GROUND_MOON = 6;
const int GROUND_LAYERS = 7;

const vec3 SEABED_DEEP = vec3(0.02, 0.05, 0.12);
const vec3 SEABED_SHALLOW = vec3(0.10, 0.22, 0.30);
const vec3 BEACH = vec3(0.62, 0.55, 0.38);
const vec3 ROCK = vec3(0.30, 0.27, 0.25);
const vec3 SNOW = vec3(0.85, 0.87, 0.90);

// Per biome: the bare ground (soil), the low plants (plant) and the forest
// seen from above (canopy), linear albedo.
const vec3 BIOME_SOIL[BIOMES] = vec3[BIOMES](
    vec3(0.50, 0.36, 0.21), vec3(0.38, 0.26, 0.14), vec3(0.21, 0.14, 0.08), vec3(0.27, 0.22, 0.14),
    vec3(0.28, 0.22, 0.14), vec3(0.22, 0.16, 0.10), vec3(0.20, 0.17, 0.13), vec3(0.17, 0.15, 0.12));
const vec3 BIOME_PLANT[BIOMES] = vec3[BIOMES](
    vec3(0.204, 0.183, 0.134), vec3(0.220, 0.185, 0.118), vec3(0.076, 0.125, 0.066), vec3(0.134, 0.130, 0.085),
    vec3(0.115, 0.160, 0.090), vec3(0.103, 0.141, 0.078), vec3(0.076, 0.104, 0.069), vec3(0.082, 0.082, 0.058));
const vec3 BIOME_CANOPY[BIOMES] = vec3[BIOMES](
    vec3(0.153, 0.148, 0.108), vec3(0.111, 0.116, 0.076), vec3(0.045, 0.080, 0.045), vec3(0.076, 0.086, 0.056),
    vec3(0.071, 0.101, 0.061), vec3(0.071, 0.101, 0.061), vec3(0.045, 0.065, 0.050), vec3(0.082, 0.087, 0.067));

// The trees' and bushes' foliage, of their biome's canopy colour (which
// takes in the shade between the crowns).
const float LEAF_FROM_CANOPY = 1.8;

// Per biome: the shares of sand, dry ground, grass, forest and rock in its
// open ground (before cover moves forest to grass).
const float BIOME_SHARES[BIOMES * 5] = float[BIOMES * 5](
    0.85, 0.15, 0.00, 0.00, 0.00,     // desert
    0.00, 0.55, 0.45, 0.00, 0.00,     // savanna
    0.00, 0.00, 0.30, 0.70, 0.00,     // tropical
    0.00, 0.55, 0.45, 0.00, 0.00,     // steppe
    0.00, 0.10, 0.90, 0.00, 0.00,     // grassland
    0.00, 0.00, 0.35, 0.65, 0.00,     // deciduous
    0.00, 0.10, 0.25, 0.65, 0.00,     // taiga
    0.00, 0.45, 0.40, 0.00, 0.15);    // tundra

// Per biome: tree chance, the conifers' share of trees, bush chance, grass
// chance, and how dry its grass is (0 green .. 1 straw).
const float BIOME_PLANTS[BIOMES * 5] = float[BIOMES * 5](
    0.004, 0.00, 0.03, 0.06, 1.00,    // desert
    0.06, 0.00, 0.10, 0.85, 0.75,     // savanna
    0.95, 0.00, 0.25, 0.35, 0.00,     // tropical
    0.01, 0.50, 0.06, 0.90, 0.60,     // steppe
    0.03, 0.20, 0.10, 0.95, 0.15,     // grassland
    0.85, 0.15, 0.12, 0.35, 0.05,     // deciduous
    0.80, 0.95, 0.08, 0.30, 0.20,     // taiga
    0.00, 1.00, 0.08, 0.50, 0.45);    // tundra

// Per biome: the acacias' share of its broadleaf trees, and the shrubs'
// and the cacti's shares of its bushes.
const float BIOME_FORMS[BIOMES * 3] = float[BIOMES * 3](
    1.0, 0.5, 0.5,      // desert
    0.9, 0.3, 0.0,      // savanna
    0.0, 0.0, 0.0,      // tropical
    0.6, 0.7, 0.05,     // steppe
    0.0, 0.3, 0.0,      // grassland
    0.0, 0.0, 0.0,      // deciduous
    0.0, 0.3, 0.0,      // taiga
    0.0, 1.0, 0.0);     // tundra

const vec3 CACTUS = vec3(0.10, 0.17, 0.07);

// The biome view's flat colours (sRGB-ish, shown as albedo).
const vec3 BIOME_VIEW[BIOMES] = vec3[BIOMES](
    vec3(0.85, 0.70, 0.30), vec3(0.75, 0.55, 0.15), vec3(0.05, 0.40, 0.10), vec3(0.65, 0.65, 0.35),
    vec3(0.40, 0.70, 0.20), vec3(0.15, 0.50, 0.15), vec3(0.10, 0.30, 0.30), vec3(0.40, 0.25, 0.45));

// The climate settings' offsets on the climate a chunk's vertices carry
// (horizon.yzw): temperature, moisture (clamped to 0 .. 1), cover.
vec3 climateWithSettings(vec3 climate) {
    return vec3(climate.x + terrain.climate.x, clamp(climate.y + terrain.climate.y, 0.0, 1.0), climate.z);
}

// `colour` turned about the grey axis by `angle` radians in YIQ, keeping
// its luminance; no turn leaves it as it is.
vec3 hueTurn(vec3 colour, float angle) {
    if (angle == 0.0) return colour;
    const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 fromYiq = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYiq * colour;
    float c = cos(angle), s = sin(angle);
    yiq.yz = vec2(c * yiq.y - s * yiq.z, s * yiq.y + c * yiq.z);
    return max(fromYiq * yiq, vec3(0.0));
}

vec3 biomeSoil(int b) { return hueTurn(BIOME_SOIL[b], terrain.climate.w); }
vec3 biomePlant(int b) { return hueTurn(BIOME_PLANT[b], terrain.climate.z); }
vec3 biomeCanopy(int b) { return hueTurn(BIOME_CANOPY[b], terrain.climate.z); }
vec3 biomeCactus() { return hueTurn(CACTUS, terrain.climate.z); }

// Weights of the four bands below, between and above three edges.
vec4 biomeBands(float x, vec3 edges, float blend) {
    vec3 s = smoothstep(edges - blend, edges + blend, vec3(x));
    return vec4(1.0 - s.x, s.x - s.y, s.y - s.z, s.z);
}

// Each biome's weight for a temperature and moisture (with the settings'
// offsets); they sum to 1.
void biomeWeights(float temperature, float moisture, out float weights[BIOMES]) {
    for (int b = 0; b < BIOMES; ++b) weights[b] = 0.0;
    vec4 t = biomeBands(temperature, BIOME_T_EDGES, BIOME_T_BLEND);
    vec4 m = biomeBands(moisture, BIOME_M_EDGES, BIOME_M_BLEND);
    for (int i = 0; i < 4; ++i) {
        if (t[i] <= 0.0) continue;
        for (int j = 0; j < 4; ++j) weights[BIOME_TABLE[i * 4 + j]] += t[i] * m[j];
    }
}

// The biome with the greatest weight.
int dominantBiome(float weights[BIOMES]) {
    int best = 0;
    for (int b = 1; b < BIOMES; ++b) {
        if (weights[b] > weights[best]) best = b;
    }
    return best;
}

// How much of the forest share stays forest at a cover: little in a
// clearing, all of it in a grove.
float coverForest(float cover) {
    return mix(0.25, 1.0, smoothstep(0.3, 0.6, cover));
}

// The ground's open share at a point: not rock, snow or beach. Plants grow
// on it alone; `radial` and `normal` give its slope (0 flat .. 1 vertical)
// and aspect, `height` is metres above sea level.
void groundCover(vec3 radial, vec3 normal, float height, float temperature, out float rock, out float snow,
                 out float beach) {
    float slope = 1.0 - dot(normal, radial);
    float cold = temperature + aspectWarmth(radial, normal);
    rock = smoothstep(0.12, 0.22, slope);
    snow = (1.0 - smoothstep(SNOW_TEMPERATURE - 1.5, SNOW_TEMPERATURE + 1.5, cold))
         * (1.0 - smoothstep(0.35, 0.5, slope));
    beach = 1.0 - smoothstep(10.0, 40.0, height);
}

// The planet's ground as weights of the layers (ground.glsl) and their
// colours: the sea floor (on the sand's texture) darker with depth; beach
// sand below 40 m; rock where the slope is steep, in its strata
// (strataShade); snow where it is cold; elsewhere each biome's shares of
// sand, dry ground, grass, forest and rock in its colours (sand its soil,
// dry ground between its soil and its plants, grass its plants, forest its
// canopy), the forest share thinned to grass where the cover is low, and
// the scree's share of all but the rock turned to dry ground in SCREE.
// `climate` is the vertices' (horizon.yzw).
void groundLayers(vec3 radial, float height, vec3 normal, vec3 climate, out float weights[GROUND_LAYERS],
                  out vec3 colours[GROUND_LAYERS]) {
    for (int i = 0; i < GROUND_LAYERS; ++i) {
        weights[i] = 0.0;
        colours[i] = vec3(0.0);
    }
    colours[GROUND_ROCK] = ROCK * strataShade(radial, height);
    colours[GROUND_SNOW] = SNOW;
    if (height < 0.0) {
        weights[GROUND_SAND] = 1.0;
        colours[GROUND_SAND] = mix(SEABED_SHALLOW, SEABED_DEEP, smoothstep(0.0, 800.0, -height));
        return;
    }
    vec3 c = climateWithSettings(climate);
    float rock, snow, beach;
    groundCover(radial, normal, height, c.x, rock, snow, beach);
    float open = (1.0 - rock) * (1.0 - snow) * (1.0 - beach);

    float biomes[BIOMES];
    biomeWeights(c.x, c.y, biomes);
    float forestKept = coverForest(coverOfClimate(c));
    vec3 sums[4] = vec3[4](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
    float shares[5] = float[5](0.0, 0.0, 0.0, 0.0, 0.0);
    for (int b = 0; b < BIOMES; ++b) {
        float w = biomes[b];
        if (w <= 0.0) continue;
        vec3 soil = biomeSoil(b), plant = biomePlant(b);
        float forest = BIOME_SHARES[b * 5 + 3];
        float s[5] = float[5](BIOME_SHARES[b * 5], BIOME_SHARES[b * 5 + 1],
                              BIOME_SHARES[b * 5 + 2] + forest * (1.0 - forestKept), forest * forestKept,
                              BIOME_SHARES[b * 5 + 4]);
        vec3 layer[4] = vec3[4](soil, mix(soil, plant, 0.35), plant, biomeCanopy(b));
        for (int i = 0; i < 4; ++i) sums[i] += w * s[i] * layer[i];
        for (int i = 0; i < 5; ++i) shares[i] += w * s[i];
    }
    for (int i = 0; i < 4; ++i) {
        weights[i] = shares[i] * open;
        colours[i] = shares[i] > 0.0 ? sums[i] / shares[i] : BEACH;
    }
    weights[GROUND_SAND] += beach * (1.0 - rock) * (1.0 - snow);
    colours[GROUND_SAND] = mix(colours[GROUND_SAND], BEACH,
                               beach / max(beach + shares[GROUND_SAND] * (1.0 - beach), 1e-4));
    weights[GROUND_ROCK] = rock * (1.0 - snow) + shares[4] * open;
    weights[GROUND_SNOW] = snow;
    float scree = screeOfClimate(c);
    if (scree > 0.0) {
        float moved = 0.0;
        for (int i = 0; i < 4; ++i) {
            moved += weights[i] * scree;
            weights[i] *= 1.0 - scree;
        }
        colours[GROUND_DRY] = mix(colours[GROUND_DRY], SCREE, moved / max(weights[GROUND_DRY] + moved, 1e-4));
        weights[GROUND_DRY] += moved;
    }
}

// The ground's colour in the biome view: each biome's flat colour by its
// weight, the sea floor dark blue, beaches, rock and snow in their own.
vec3 biomeViewColour(vec3 radial, float height, vec3 normal, vec3 climate) {
    if (height < 0.0) return vec3(0.02, 0.05, 0.25);
    vec3 c = climateWithSettings(climate);
    float rock, snow, beach;
    groundCover(radial, normal, height, c.x, rock, snow, beach);
    float biomes[BIOMES];
    biomeWeights(c.x, c.y, biomes);
    vec3 colour = vec3(0.0);
    for (int b = 0; b < BIOMES; ++b) colour += biomes[b] * BIOME_VIEW[b];
    colour = mix(colour, vec3(0.9, 0.85, 0.6), beach);
    colour = mix(colour, vec3(0.35), rock);
    return mix(colour, vec3(1.0), snow);
}

// What grows at a point, the biomes' values weighted: x the tree chance,
// y the conifers' share of trees, z the bush chance, w the grass chance,
// all times the open share (not rock, snow, beach, scree or sea floor) and
// trees times the cover's grove or clearing; `leaf` the trees' and bushes'
// foliage colour (the canopy's times LEAF_FROM_CANOPY) and `grass` the
// grass's (its ground's plants, towards the soil by how dry the biome's
// grass is), on the palette; `forms` the biomes' BIOME_FORMS weighted.
vec4 biomePlants(vec3 radial, float height, vec3 normal, vec3 climate, out vec3 leaf, out vec3 grass,
                 out vec3 forms) {
    leaf = vec3(0.0);
    grass = vec3(0.0);
    forms = vec3(0.0);
    if (height < 0.0) return vec4(0.0);
    vec3 c = climateWithSettings(climate);
    float rock, snow, beach;
    groundCover(radial, normal, height, c.x, rock, snow, beach);
    float open = (1.0 - rock) * (1.0 - snow) * (1.0 - beach) * (1.0 - screeOfClimate(c));
    float biomes[BIOMES];
    biomeWeights(c.x, c.y, biomes);
    vec4 plants = vec4(0.0);
    for (int b = 0; b < BIOMES; ++b) {
        float w = biomes[b];
        if (w <= 0.0) continue;
        plants += w * vec4(BIOME_PLANTS[b * 5], BIOME_PLANTS[b * 5 + 1], BIOME_PLANTS[b * 5 + 2],
                           BIOME_PLANTS[b * 5 + 3]);
        leaf += w * biomeCanopy(b) * LEAF_FROM_CANOPY;
        forms += w * vec3(BIOME_FORMS[b * 3], BIOME_FORMS[b * 3 + 1], BIOME_FORMS[b * 3 + 2]);
        grass += w * mix(biomePlant(b), mix(biomeSoil(b), biomePlant(b), 0.5), BIOME_PLANTS[b * 5 + 4] * 0.5);
    }
    plants.x *= mix(0.2, 1.4, smoothstep(0.25, 0.75, coverOfClimate(c)));
    plants.xzw *= open;
    return plants;
}
