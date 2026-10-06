// csm.glsl - filtered lookups in the sun's cascaded shadow map.
//
// The including shader declares, before including this file:
//   uniform sampler2DArrayShadow shadowMap;       one cascade per layer
//   a Cascades block instance named `cascades`    (DEFAULT_CASCADES_BLOCK)
//   a Settings block instance named `settings`    (DEFAULT_SETTINGS_BLOCK)

#ifndef DEFAULT_CSM_GLSL
#define DEFAULT_CSM_GLSL

#include "common.glsl"

// 16 points of a Vogel spiral, evenly covering the unit disc.
vec2 vogelDisk(int i, int count, float rotation) {
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * 2.39996323 + rotation;
    return r * vec2(cos(theta), sin(theta));
}

// Fraction of the sun reaching worldPos in cascade c, 0 = fully shadowed.
float cascadeVisibility(int c, vec3 worldPos, vec3 N, float NdotL, float noise) {
    // Push the lookup off the surface along its normal, by more where the
    // light grazes it, in units of this cascade's shadow texel.
    float sinL = sqrt(max(1.0 - NdotL * NdotL, 0.0));
    vec3 offsetPos = worldPos + N * (cascades.texelWorldSize[c] * settings.shadowNormalBias * (0.5 + sinL));

    vec4 clip = cascades.cascadeViewProj[c] * vec4(offsetPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) {
        return 1.0;
    }

    // The same world-space filter width in every cascade: softness is in
    // texels of the first cascade, so later, coarser cascades use fewer.
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float radius = max(settings.shadowSoftness * cascades.texelWorldSize[0] / cascades.texelWorldSize[c], 1.0);
    float reference = ndc.z - settings.shadowDepthBias;
    float rotation = noise * 6.28318531;

    const int TAPS = 16;
    float lit = 0.0;
    for (int i = 0; i < TAPS; ++i) {
        vec2 offset = vogelDisk(i, TAPS, rotation) * radius * texel;
        lit += texture(shadowMap, vec4(uv + offset, float(c), reference));
    }
    return lit / float(TAPS);
}

// Fraction of the sun reaching worldPos, blending into the next cascade near
// the end of each. viewDepth is the positive distance along the view axis.
// Returns the cascade used in `cascade` (-1 past the last one), for debug views.
float sunVisibility(vec3 worldPos, vec3 N, vec3 L, float viewDepth, float noise, out int cascade) {
    cascade = -1;
    if (cascades.sunEnabled == 0u || cascades.cascadeCount == 0u) return 1.0;

    int count = int(min(cascades.cascadeCount, 4u));
    int c = 0;
    while (c < count && viewDepth > cascades.splits[c]) ++c;
    if (c >= count) return 1.0;
    cascade = c;

    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float visibility = cascadeVisibility(c, worldPos, N, NdotL, noise);

    // Across the last `cascadeBlend` of this cascade's slice, fade into the
    // next one (or into no shadow after the last), so the seam is not a line.
    float start = c == 0 ? 0.0 : cascades.splits[c - 1];
    float end = cascades.splits[c];
    float blendStart = end - (end - start) * settings.cascadeBlend;
    if (settings.cascadeBlend > 0.0 && viewDepth > blendStart) {
        float t = smoothstep(blendStart, end, viewDepth);
        float next = c + 1 < count ? cascadeVisibility(c + 1, worldPos, N, NdotL, noise) : 1.0;
        visibility = mix(visibility, next, t);
    }
    return visibility;
}

#endif
