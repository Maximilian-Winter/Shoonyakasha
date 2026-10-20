// vsm_sample.glsl - filtered lookups in the sun's virtual shadow map.
//
// The including shader declares, before including this file:
//   the Showroom block as `showroom`                       (SHOWROOM_BLOCK)
//   readonly r32ui uimage2D vsmPageTable and vsmPhysical    (vsmSet bindings 0 and 2)

#ifndef SHOWROOM_VSM_SAMPLE_GLSL
#define SHOWROOM_VSM_SAMPLE_GLSL

#include "vsm.glsl"

// Points of a Vogel spiral, evenly covering the unit disc.
vec2 vsmVogel(int i, int count, float rotation) {
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * 2.39996323 + rotation;
    return r * vec2(cos(theta), sin(theta));
}

// Fraction of the sun reaching worldPos. pixelWorld is the world size of
// the pixel it is seen through (vsmPixelWorld), which picks the level; noise
// in [0, 1) turns the filter, so TAA averages it smooth. Returns the level
// read in `level` (-1 for none) and whether its page was drawn this frame in
// `drawn`, for the debug views.
float vsmVisibility(vec3 worldPos, vec3 N, float pixelWorld, float noise, out int level, out bool drawn) {
    level = -1;
    drawn = false;
    if (showroom.sunEnabled == 0u) return 1.0;

    VsmBasis b = vsmBasis();
    int first = vsmSelectLevel(b, worldPos, pixelWorld);
    if (first < 0) return 1.0;

    // Normally the page is there, asked for by this very pixel. Blended
    // surfaces are not in the depth buffer and ask for nothing; they fall
    // back to the nearest coarser level that has their page.
    for (int lv = first; lv < VSM_LEVELS; ++lv) {
        vec3 p = vsmOffsetPosition(worldPos, N, b.toSun, lv);
        vec3 ls = vsmLightSpace(b, p);
        vec2 texel = ls.xy / vsmTexelSize(lv);
        ivec2 page = ivec2(floor(texel / float(VSM_PAGE_SIZE)));
        if (!vsmInWindow(page, vsmWindowOrigin(b, lv))) continue;
        uint entry = imageLoad(vsmPageTable, vsmEntryCoord(lv, page)).r;
        if ((entry & VSM_VALID) == 0u) continue;

        level = lv;
        drawn = (entry & VSM_DIRTY) != 0u;
        float bias = (vsmTexelSize(lv) + showroom.vsmDepthBias) / (2.0 * showroom.vsmDepthRange);
        float reference = vsmDepth(ls.z) - bias;

        const int TAPS = 12;
        float rotation = noise * 6.28318531;
        float lit = 0.0, counted = 0.0;
        ivec2 cachedPage = page;
        uint cachedEntry = entry;
        for (int i = 0; i < TAPS; ++i) {
            vec2 t = texel + vsmVogel(i, TAPS, rotation) * showroom.vsmSoftness;
            ivec2 tp = ivec2(floor(t / float(VSM_PAGE_SIZE)));
            if (tp != cachedPage) {
                cachedPage = tp;
                cachedEntry = imageLoad(vsmPageTable, vsmEntryCoord(lv, tp)).r;
            }
            if ((cachedEntry & VSM_VALID) == 0u) continue;
            ivec2 local = ivec2(floor(t)) - tp * VSM_PAGE_SIZE;
            float occluder = uintBitsToFloat(
                imageLoad(vsmPhysical, vsmPhysicalPage(cachedEntry) * VSM_PAGE_SIZE + local).r);
            lit += reference <= occluder ? 1.0 : 0.0;
            counted += 1.0;
        }
        return counted > 0.0 ? lit / counted : 1.0;
    }
    return 1.0;
}

// The level and drawn flag, packed into the shadow mask's green channel for
// the debug views: (level + 8 if drawn this frame) / 16, 1 for none.
float vsmDebugCode(int level, bool drawn) {
    return level < 0 ? 1.0 : (float(level) + (drawn ? 8.0 : 0.0)) / 16.0;
}

#endif
