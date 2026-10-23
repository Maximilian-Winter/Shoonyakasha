// csm.glsl - filtered lookups in the suns' cascaded shadow map.
//
// The including shader declares, before including this file:
//   uniform sampler2DArrayShadow shadowMap;       one cascade per layer: sun s's
//                                                 cascade c in layer 4 * s + c
//   a Cascades block instance named `cascades`    (DEFAULT_CASCADES_BLOCK)
//   a Settings block instance named `settings`    (DEFAULT_SETTINGS_BLOCK)

#ifndef DEFAULT_CSM_GLSL
#define DEFAULT_CSM_GLSL

#include "common.glsl"

// Sun s's (0 or 1) values from the Cascades block.
vec4 sunSplits(int s) { return s == 0 ? cascades.splits : cascades.secondSplits; }
vec4 sunTexelWorldSize(int s) { return s == 0 ? cascades.texelWorldSize : cascades.secondTexelWorldSize; }
vec3 sunTravel(int s) { return (s == 0 ? cascades.sunDirection : cascades.secondSunDirection).xyz; }
int sunLight(int s) {
    if (s == 0) return cascades.sunEnabled != 0u ? cascades.sunLightIndex : -1;
    return cascades.secondSunEnabled != 0u ? cascades.secondSunLightIndex : -1;
}
int sunCascadeCount(int s) {
    uint enabled = s == 0 ? cascades.sunEnabled : cascades.secondSunEnabled;
    uint count = s == 0 ? cascades.cascadeCount : cascades.secondCascadeCount;
    return enabled != 0u ? int(min(count, 4u)) : 0;
}

// 16 points of a Vogel spiral, evenly covering the unit disc.
vec2 vogelDisk(int i, int count, float rotation) {
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * 2.39996323 + rotation;
    return r * vec2(cos(theta), sin(theta));
}

// Fraction of sun s reaching worldPos in its cascade c, 0 = fully shadowed.
float cascadeVisibility(int s, int c, vec3 worldPos, vec3 N, float NdotL, float noise) {
    vec4 texelWorldSize = sunTexelWorldSize(s);
    int layer = 4 * s + c;
    // Push the lookup off the surface along its normal, by more where the
    // light grazes it, in units of this cascade's shadow texel.
    float sinL = sqrt(max(1.0 - NdotL * NdotL, 0.0));
    vec3 offsetPos = worldPos + N * (texelWorldSize[c] * settings.shadowNormalBias * (0.5 + sinL));

    vec4 clip = cascades.cascadeViewProj[layer] * vec4(offsetPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) {
        return 1.0;
    }

    // The same world-space filter width in every cascade: softness is in
    // texels of the first cascade, so later, coarser cascades use fewer.
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float radius = max(settings.shadowSoftness * texelWorldSize[0] / texelWorldSize[c], 1.0);
    float reference = ndc.z - settings.shadowDepthBias;
    float rotation = noise * 6.28318531;

    const int TAPS = 16;
    float lit = 0.0;
    for (int i = 0; i < TAPS; ++i) {
        vec2 offset = vogelDisk(i, TAPS, rotation) * radius * texel;
        lit += texture(shadowMap, vec4(uv + offset, float(layer), reference));
    }
    return lit / float(TAPS);
}

// Fraction of sun s (0 or 1) reaching worldPos, blending into the next
// cascade near the end of each; 1 when the sun has no light. viewDepth is the
// positive distance along the view axis. Returns the cascade used in
// `cascade` (-1 past the last one), for debug views.
float sunVisibility(int s, vec3 worldPos, vec3 N, vec3 L, float viewDepth, float noise, out int cascade) {
    cascade = -1;
    int count = sunCascadeCount(s);
    if (count == 0) return 1.0;

    vec4 splits = sunSplits(s);
    int c = 0;
    while (c < count && viewDepth > splits[c]) ++c;
    if (c >= count) return 1.0;
    cascade = c;

    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float visibility = cascadeVisibility(s, c, worldPos, N, NdotL, noise);

    // Across the last `cascadeBlend` of this cascade's slice, fade into the
    // next one (or into no shadow after the last), so the seam is not a line.
    float start = c == 0 ? 0.0 : splits[c - 1];
    float end = splits[c];
    float blendStart = end - (end - start) * settings.cascadeBlend;
    if (settings.cascadeBlend > 0.0 && viewDepth > blendStart) {
        float t = smoothstep(blendStart, end, viewDepth);
        float next = c + 1 < count ? cascadeVisibility(s, c + 1, worldPos, N, NdotL, noise) : 1.0;
        visibility = mix(visibility, next, t);
    }
    return visibility;
}

// The first sun's visibility, as above.
float sunVisibility(vec3 worldPos, vec3 N, vec3 L, float viewDepth, float noise, out int cascade) {
    return sunVisibility(0, worldPos, N, L, viewDepth, noise, cascade);
}

#endif
