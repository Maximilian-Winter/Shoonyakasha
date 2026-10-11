// vtfs_lights.glsl - light from the VTFS point lights listed for a cluster,
// for the forward passes. The including shader includes common.glsl,
// vtfs.glsl and sk/pbr.glsl and defines VTFS_SET, the set of pipeline.json's
// vtfsShadeSet.

#ifndef VTFS_LIGHTS_GLSL
#define VTFS_LIGHTS_GLSL

layout(set = VTFS_SET, binding = 0) uniform Vtfs { VTFS_BLOCK } vtfs;
layout(std430, set = VTFS_SET, binding = 1) readonly buffer VtfsLightGrid { uvec2 lightGrid[]; };
layout(std430, set = VTFS_SET, binding = 2) readonly buffer VtfsLightIndexList { uint lightIndexList[]; };
layout(std430, set = VTFS_SET, binding = 3) readonly buffer VtfsLightsWorld { vec4 lightsWorld[]; };
layout(std430, set = VTFS_SET, binding = 4) readonly buffer VtfsLightsColor { vec4 lightsColor[]; };

// Lights listed for `cluster`.
uint vtfsLightsIn(uint cluster) {
    return lightGrid[cluster].y;
}

// Light from VTFS light i: an inverse-square falloff, windowed smoothly to
// zero at the light's range.
vec3 vtfsLightFrom(uint i, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                   vec3 F0) {
    vec4 light = lightsWorld[i];
    vec3 toLight = light.xyz - worldPos;
    float distanceSquared = dot(toLight, toLight);
    float rangeSquared = light.w * light.w;
    if (distanceSquared >= rangeSquared) return vec3(0.0);
    vec3 L = toLight * inversesqrt(max(distanceSquared, 1e-8));
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return vec3(0.0);
    float window = 1.0 - (distanceSquared * distanceSquared) / (rangeSquared * rangeSquared);
    float falloff = window * window / max(distanceSquared, 0.01);
    return cookTorrance(N, V, L, albedo, metallic, roughness, F0) * lightsColor[i].rgb * NdotL * falloff;
}

// Light from every VTFS light listed for `cluster`.
vec3 vtfsDirectLight(uint cluster, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic,
                     float roughness, vec3 F0) {
    uvec2 entry = lightGrid[cluster];
    vec3 total = vec3(0.0);
    for (uint k = 0u; k < entry.y; ++k) {
        total += vtfsLightFrom(lightIndexList[entry.x + k], worldPos, N, V, albedo, metallic, roughness, F0);
    }
    return total;
}

// Debug view, a reference: light from every VTFS light, clusters or not.
// As slow as forward shading without culling; meant for a few thousand
// lights.
vec3 vtfsDirectLightEvery(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                          vec3 F0) {
    vec3 total = vec3(0.0);
    for (uint i = 0u; i < min(vtfs.lightCount, VTFS_MAX_LIGHTS); ++i) {
        total += vtfsLightFrom(i, worldPos, N, V, albedo, metallic, roughness, F0);
    }
    return total;
}

// Debug view: lights in the cluster, blue none, green half of vtfs.heatScale,
// red the whole; magenta where the cluster's list is full.
vec3 vtfsHeat(uint cluster) {
    uint count = vtfsLightsIn(cluster);
    if (count >= VTFS_MAX_PER_CLUSTER) return vec3(1.0, 0.0, 1.0);
    float n = float(count) / max(vtfs.heatScale, 1.0);
    return clamp(vec3(n * 2.0 - 1.0, 1.0 - abs(n * 2.0 - 1.0), 1.0 - n * 2.0), 0.0, 1.0);
}

// Debug view: each cluster in its own colour.
vec3 vtfsClusterColor(uint cluster) {
    uint h = cluster * 2654435761u;
    return vec3((h >> 8) & 255u, (h >> 16) & 255u, (h >> 24) & 255u) / 255.0;
}

#endif
