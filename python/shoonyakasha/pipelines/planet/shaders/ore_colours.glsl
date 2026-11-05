// An ore rock's colours by resource id (resources.glsl's RESOURCE_ ids):
// ORE_TINTS, the colour its rock colour is mixed towards, and ORE_SPECKS,
// its glowing specks' colour. shoonyakasha.planet.rocks.ORE_TINTS and
// SPECK_COLOURS hold the same.

const int ORE_COLOURS = 8;

const vec3 ORE_TINTS[ORE_COLOURS] = vec3[ORE_COLOURS](
    vec3(0.46, 0.25, 0.13),     // none
    vec3(0.46, 0.25, 0.13),     // ferrite
    vec3(0.30, 0.42, 0.33),     // cuprite
    vec3(0.78, 0.80, 0.84),     // quartz
    vec3(0.36, 0.22, 0.10),     // amber
    vec3(0.85, 0.80, 0.76),     // evaporite
    vec3(0.62, 0.72, 0.82),     // ice
    vec3(0.07, 0.07, 0.08));    // ilmenite

const vec3 ORE_SPECKS[ORE_COLOURS] = vec3[ORE_COLOURS](
    vec3(1.00, 0.72, 0.28),     // none
    vec3(1.00, 0.50, 0.20),     // ferrite
    vec3(0.35, 1.00, 0.55),     // cuprite
    vec3(0.65, 0.85, 1.00),     // quartz
    vec3(1.00, 0.78, 0.28),     // amber
    vec3(1.00, 0.60, 0.75),     // evaporite
    vec3(0.70, 0.90, 1.00),     // ice
    vec3(0.80, 0.85, 0.90));    // ilmenite

vec3 oreTint(uint resourceId) {
    return ORE_TINTS[min(resourceId, uint(ORE_COLOURS - 1))];
}

vec3 oreSpeckColour(uint resourceId) {
    return ORE_SPECKS[min(resourceId, uint(ORE_COLOURS - 1))];
}
