// The moon's terrain: moonHeight(dir, radius), metres above the sphere of
// `radius` at the unit direction `dir` from the moon's centre. Include
// after terrain.glsl (terrainHash, fbm3).
//
// Craters of MOON_CRATER_OCTAVES sizes on rolling ground. For each size the
// scaled direction p = dir * cells lies in a lattice of unit cells; a cell
// within half a cell of the sphere of radius `cells` holds a crater with
// chance MOON_CRATER_CHANCE, centred at a hashed point of the cell moved
// onto that sphere, its radius a hashed share of a cell between
// MOON_CRATER_SIZE.x and .y. A crater reaches 1 + MOON_RIM_WIDTH of its
// radius, less than half a cell, so only the 2 x 2 x 2 cells around p can
// reach it. Each size has MOON_CRATER_LACUNARITY times the cells of the one
// before. Overlapping craters add.
//
// A crater's shape, at x = distance / radius (after Sebastian Lague's
// planet craters): the bowl x^2 - 1, raised to a floor at MOON_FLOOR by a
// smooth maximum, cut by the rim MOON_RIM_STEEPNESS * min(x - 1 -
// MOON_RIM_WIDTH, 0)^2 by a smooth minimum; 0 beyond the rim. Its height
// in metres is the shape times MOON_CRATER_DEPTH times the radius.
//
// moonHeight takes `smallest` (m): the sizes of craters whose largest
// radius is below it, and the fine relief when its wavelength is, are
// left out (0 keeps them all).

const int MOON_CRATER_OCTAVES = 7;
const float MOON_CRATER_CELLS = 6.0;          // cells per unit of direction for the largest craters
const float MOON_CRATER_LACUNARITY = 2.2;
const float MOON_CRATER_CHANCE = 0.6;
const vec2 MOON_CRATER_SIZE = vec2(0.08, 0.28);   // radius, in cells
const float MOON_CRATER_DEPTH = 0.4;          // metres of shape per metre of radius
const float MOON_FLOOR = -0.6;
const float MOON_RIM_WIDTH = 0.7;
const float MOON_RIM_STEEPNESS = 0.42;
const float MOON_SMOOTHNESS = 0.3;
// Rolling ground: fBm at 2 cells per unit of direction (about 12 km
// across), and fine relief at 20 m wavelength.
const float MOON_HILLS = 400.0;
const float MOON_RELIEF = 1.5;
const float MOON_RELIEF_WAVELENGTH = 20.0;

float moonSmoothMin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float moonCraterShape(float x) {
    float bowl = x * x - 1.0;
    float rimX = min(x - 1.0 - MOON_RIM_WIDTH, 0.0);
    float rim = MOON_RIM_STEEPNESS * rimX * rimX;
    float shape = -moonSmoothMin(-bowl, -MOON_FLOOR, MOON_SMOOTHNESS);
    return moonSmoothMin(shape, rim, MOON_SMOOTHNESS);
}

// Unit float from a hash.
float moonUnit(uint h) {
    return float(h & 0xffffffu) / 16777216.0;
}

float moonCraters(vec3 dir, float radius, float smallest) {
    float height = 0.0;
    float cells = MOON_CRATER_CELLS;
    for (int octave = 0; octave < MOON_CRATER_OCTAVES; ++octave) {
        vec3 p = dir * cells;
        ivec3 first = ivec3(floor(p - 0.5));
        float metresPerCell = radius / cells;
        if (MOON_CRATER_SIZE.y * metresPerCell < smallest) break;
        for (int k = 0; k < 8; ++k) {
            ivec3 c = first + ivec3(k & 1, (k >> 1) & 1, k >> 2);
            uint h = terrainHash(c + ivec3(octave * 7919, octave * 104729, 0));
            if (moonUnit(h) >= MOON_CRATER_CHANCE) continue;
            vec3 centre = vec3(c) + vec3(moonUnit(terrainHash(ivec3(int(h), 1, 0))),
                                         moonUnit(terrainHash(ivec3(int(h), 2, 0))),
                                         moonUnit(terrainHash(ivec3(int(h), 3, 0))));
            float r = length(centre);
            if (abs(r - cells) > 0.5) continue;
            centre *= cells / r;
            float size = mix(MOON_CRATER_SIZE.x, MOON_CRATER_SIZE.y, moonUnit(terrainHash(ivec3(int(h), 4, 0))));
            float x = length(p - centre) / size;
            if (x >= 1.0 + MOON_RIM_WIDTH) continue;
            height += moonCraterShape(x) * MOON_CRATER_DEPTH * size * metresPerCell;
        }
        cells *= MOON_CRATER_LACUNARITY;
    }
    return height;
}

float moonHeight(vec3 dir, float radius, float smallest) {
    float hills = fbm3(dir * 2.0 + vec3(41.3, 17.9, 5.1), 5, 2.0, 0.5) * MOON_HILLS;
    float relief = smallest > MOON_RELIEF_WAVELENGTH ? 0.0
                 : fbm3(dir * (radius / MOON_RELIEF_WAVELENGTH), 3, 2.0, 0.5) * MOON_RELIEF;
    return hills + relief + moonCraters(dir, radius, smallest);
}
