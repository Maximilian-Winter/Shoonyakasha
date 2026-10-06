// local_shadows.glsl - how much of a spot or point light reaches a point,
// from their shadow maps: tiles of one atlas, a tile per spot light and one
// per face of a point light's cube. Included by lights.glsl, after the
// Lights block, when the including shader defines LOCAL_SHADOWS and
// declares, before including lights.glsl:
//   uniform LocalShadowMatrices { DEFAULT_LOCAL_SHADOWS_BLOCK } localShadows;
//   uniform sampler2DShadow localShadowAtlas;
//   a Settings block instance named `settings`      (DEFAULT_SETTINGS_BLOCK)

#ifndef DEFAULT_LOCAL_SHADOWS_GLSL
#define DEFAULT_LOCAL_SHADOWS_GLSL

// 3x3 hardware-filtered comparisons around `uv` (0..1 across the tile) in
// tile `rect`, kept inside the tile so no neighbour's depth bleeds in.
float atlasShadow(vec4 rect, vec2 uv, float reference) {
    vec2 texel = 1.0 / vec2(textureSize(localShadowAtlas, 0));
    vec2 lo = rect.xy + texel * 0.5, hi = rect.xy + rect.zw - texel * 0.5;
    vec2 center = rect.xy + uv * rect.zw;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(localShadowAtlas, vec3(clamp(center + vec2(x, y) * texel, lo, hi), reference));
    return lit / 9.0;
}

// Moved off the surface along its normal by a few texels of the map, more at
// grazing angles.
vec3 normalOffset(vec3 worldPos, vec3 N, vec3 L, float texel) {
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    return worldPos + N * texel * settings.shadowNormalBias * (0.5 + sqrt(1.0 - NdotL * NdotL));
}

float spotShadow(int slot, vec3 lightPos, vec3 worldPos, vec3 N, vec3 L) {
    vec4 rect = localShadows.spotRect[slot];
    if (rect.z <= 0.0) return 1.0;
    float texel = localShadows.spotParams[slot].x * length(worldPos - lightPos);
    vec4 clip = localShadows.spotViewProj[slot] * vec4(normalOffset(worldPos, N, L, texel), 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) return 1.0;
    return atlasShadow(rect, uv, ndc.z - settings.shadowDepthBias);
}

// The cube face a direction falls on, in the order +X, -X, +Y, -Y, +Z, -Z.
int cubeFace(vec3 r) {
    vec3 a = abs(r);
    if (a.x >= a.y && a.x >= a.z) return r.x > 0.0 ? 0 : 1;
    if (a.y >= a.z) return r.y > 0.0 ? 2 : 3;
    return r.z > 0.0 ? 4 : 5;
}

// Through the face's own matrix, so the lookup lands where that face's pass
// rendered.
float pointShadow(int slot, vec3 worldPos, vec3 N, vec3 L) {
    vec4 positionFar = localShadows.pointPositionFar[slot];
    float texel = localShadows.pointDepthParams[slot].z * length(worldPos - positionFar.xyz);
    vec3 offsetPos = normalOffset(worldPos, N, L, texel);
    vec3 r = offsetPos - positionFar.xyz;
    vec3 a = abs(r);
    if (max(a.x, max(a.y, a.z)) >= positionFar.w) return 1.0;   // beyond the map's far plane

    int face = slot * 6 + cubeFace(r);
    vec4 rect = localShadows.pointFaceRect[face];
    if (rect.z <= 0.0) return 1.0;
    vec4 clip = localShadows.pointFaceViewProj[face] * vec4(offsetPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    return atlasShadow(rect, clamp(ndc.xy * 0.5 + 0.5, 0.0, 1.0), ndc.z - settings.shadowDepthBias);
}

// Shadowing of scene light `lightIndex`, 1 when it has no shadow slot.
float localShadowVisibility(int lightIndex, float type, vec3 worldPos, vec3 N, vec3 L) {
    if (type > 1.5) {
        for (int s = 0; s < SPOT_SHADOW_SLOTS; ++s)
            if (localShadows.spotLightIndex[s] == lightIndex)
                return spotShadow(s, lightsPositionType[lightIndex].xyz, worldPos, N, L);
    } else {
        for (int s = 0; s < POINT_SHADOW_SLOTS; ++s)
            if (localShadows.pointLightIndex[s] == lightIndex) return pointShadow(s, worldPos, N, L);
    }
    return 1.0;
}

#endif
