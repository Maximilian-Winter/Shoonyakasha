// local_shadows.glsl - how much of a spot or point light reaches a point,
// from their shadow maps. Included by lights.glsl, after the Lights block,
// when the including shader defines LOCAL_SHADOWS and declares, before
// including lights.glsl:
//   uniform LocalShadowMatrices { DEFAULT_LOCAL_SHADOWS_BLOCK } localShadows;
//   uniform sampler2DArrayShadow spotShadowMap;     one layer per spot slot
//   uniform samplerCubeArrayShadow pointShadowMap;  one cube per point slot
//   a Settings block instance named `settings`      (DEFAULT_SETTINGS_BLOCK)

#ifndef DEFAULT_LOCAL_SHADOWS_GLSL
#define DEFAULT_LOCAL_SHADOWS_GLSL

// 3x3 hardware-filtered comparisons around the point's texel in layer `slot`.
float spotShadow(int slot, vec3 lightPos, vec3 worldPos, vec3 N, vec3 L) {
    float distance = length(worldPos - lightPos);
    float texel = localShadows.spotParams[slot].x * distance;
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    vec3 offsetPos = worldPos + N * texel * settings.shadowNormalBias * (0.5 + sqrt(1.0 - NdotL * NdotL));

    vec4 clip = localShadows.spotViewProj[slot] * vec4(offsetPos, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) return 1.0;

    vec2 step = 1.0 / vec2(textureSize(spotShadowMap, 0).xy);
    float reference = ndc.z - settings.shadowDepthBias;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(spotShadowMap, vec4(uv + vec2(x, y) * step, float(slot), reference));
    return lit / 9.0;
}

// 3x3 hardware-filtered comparisons around the direction to the point, in
// cube `slot`.
float pointShadow(int slot, vec3 worldPos, vec3 N, vec3 L) {
    vec4 positionFar = localShadows.pointPositionFar[slot];
    vec4 depthParams = localShadows.pointDepthParams[slot];
    float distance = length(worldPos - positionFar.xyz);
    float texel = depthParams.z * distance;
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    vec3 offsetPos = worldPos + N * texel * settings.shadowNormalBias * (0.5 + sqrt(1.0 - NdotL * NdotL));

    vec3 r = offsetPos - positionFar.xyz;
    vec3 a = abs(r);
    float m = max(a.x, max(a.y, a.z));
    if (m >= positionFar.w) return 1.0;   // beyond the map's far plane
    float reference = depthParams.x + depthParams.y / m - settings.shadowDepthBias;

    // Two directions across the face, a texel apart at this distance.
    vec3 axis = a.x >= a.y && a.x >= a.z ? vec3(1, 0, 0) : (a.y >= a.z ? vec3(0, 1, 0) : vec3(0, 0, 1));
    vec3 u = normalize(cross(axis, abs(axis.y) > 0.5 ? vec3(1, 0, 0) : vec3(0, 1, 0)));
    vec3 v = cross(axis, u);
    u *= texel;
    v *= texel;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(pointShadowMap, vec4(r + u * float(x) + v * float(y), float(slot)), reference);
    return lit / 9.0;
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
