// The planet's geology on its ground: rock strata, snow by the slope's
// aspect, scree under cliffs and wet shores. Include after terrain.glsl.
//
// Strata (strataShade): the rock's colour scaled in bands STRATA_WIDE m
// and STRATA_NARROW m high, bent up and down by up to about STRATA_WARP m
// by a noise STRATA_WAVELENGTH m across; each wide band a little lighter
// or darker than the next.
//
// Aspect (aspectWarmth): degrees C a slope counts as warmer for its snow,
// up to ASPECT_WARMTH where it faces the equator (where the sun stands on
// average over the day) at a slope of ASPECT_FULL (the normal's sine
// from the vertical) or more, as much colder facing the pole; none on
// flat ground or near the equator.
//
// Scree (screeShare, screeOfClimate): chunk_build.comp measures the
// ground SCREE_NEAR and SCREE_FAR metres uphill of each vertex; where it
// rises like a cliff (SCREE_STEEP, rise over run) on a moderate slope
// below it, the vertex gets scree, stored in its climate's cover (z) as
// minus its share; a cover of 0 or more is the cover itself.
//
// Wet shores (wetShore): the ground within about WET_HEIGHT m above the
// water, its upper edge moved by a noise, darkened by WET_DARKEN.

#ifndef GEOLOGY_GLSL
#define GEOLOGY_GLSL

const float STRATA_WIDE = 23.0;          // m
const float STRATA_NARROW = 7.0;         // m
const float STRATA_WARP = 40.0;          // m
const float STRATA_WAVELENGTH = 800.0;   // m
const float ASPECT_WARMTH = 2.5;         // degrees C: about 300 m of the climate's lapse (climate.glsl)
const float ASPECT_FULL = 0.5;
const float SCREE_NEAR = 15.0;           // m uphill
const float SCREE_FAR = 35.0;            // m uphill
const vec2 SCREE_STEEP = vec2(0.6, 1.0); // rise over run uphill from no scree to full
const float SCREE_LEAST = 0.02;          // shares below this are left as the cover
const vec3 SCREE = vec3(0.36, 0.35, 0.33);
const float WET_HEIGHT = 2.0;            // m
const float WET_DARKEN = 0.35;

float strataBandShade(float band) {
    return float(terrainHash(ivec3(int(band), 41, 7)) & 1023u) / 1023.0 - 0.5;
}

// The factor the rock's colour takes at unit direction `dir`, `height` m above sea level.
float strataShade(vec3 dir, float height) {
    vec3 p = dir * (terrain.siteShape.z / STRATA_WAVELENGTH) + fract(terrain.seed.xyz * 0.0613) * 16.0;
    float h = height + STRATA_WARP * fbm3(p, 3, 2.0, 0.5);
    float wide = h / STRATA_WIDE, narrow = h / STRATA_NARROW;
    return 0.9 + 0.08 * sin(6.2831853 * wide) + 0.12 * strataBandShade(floor(wide))
         + 0.05 * sin(6.2831853 * narrow);
}

// Degrees C the ground at `radial` with `normal` counts as warmer for its snow.
float aspectWarmth(vec3 radial, vec3 normal) {
    vec3 pole = vec3(0.0, 1.0, 0.0) - radial * radial.y;
    float reach = length(pole);
    if (reach < 1e-4) return 0.0;
    vec3 equatorward = -sign(radial.y) * pole / reach;
    vec3 facing = normal - radial * dot(normal, radial);
    float latitude = smoothstep(0.05, 0.3, abs(radial.y));
    return ASPECT_WARMTH * latitude * clamp(dot(facing, equatorward) / ASPECT_FULL, -1.0, 1.0);
}

// The scree's share at a vertex whose slope is `slope` (0 flat .. 1
// vertical) and whose ground rises `rise` over run uphill of it.
float screeShare(float slope, float rise) {
    return smoothstep(SCREE_STEEP.x, SCREE_STEEP.y, rise) * smoothstep(0.015, 0.04, slope)
         * (1.0 - smoothstep(0.12, 0.2, slope));
}

// A climate's cover (z) with the scree's share folded in, and back.
float coverWithScree(float cover, float scree) {
    return scree >= SCREE_LEAST ? -scree : cover;
}

float screeOfClimate(vec3 climate) {
    return clamp(-climate.z, 0.0, 1.0);
}

float coverOfClimate(vec3 climate) {
    return max(climate.z, 0.0);
}

// How wet the ground is `above` m over the water's surface at unit direction `dir`: 0 to 1.
float wetShore(vec3 dir, float above) {
    if (above < 0.0) return 0.0;
    vec3 p = dir * (terrain.siteShape.z / 9.0) + fract(terrain.seed.xyz * 0.0271) * 16.0;
    float edge = WET_HEIGHT * (1.0 + 0.4 * noise3(p));
    return 1.0 - smoothstep(0.6 * edge, edge, above);
}

#endif
