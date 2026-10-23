// lights.glsl - direct light from scene.lights[i], shared by the lighting
// and forward passes. The including shader declares the Lights block
// without an instance name (DEFAULT_LIGHTS_BLOCK) and the lights themselves
// (DEFAULT_SCENE_LIGHTS), and includes sk/pbr.glsl.
// With CLUSTERED defined, directLight takes the fragment's cluster and loops
// over its lights only; with LOCAL_SHADOWS, spot and point lights are
// shadowed.
//
// Point and spot lights with a rectangle (sceneLights[i].shape: its right
// axis times half its width, and half its height) are shaded as that
// rectangle (area_lights.glsl): their highlights take its shape. It faces
// the light's direction and lights nothing behind it. The including shader
// also declares brdfLUT and the LtcTable buffer (area_lights.glsl).
//
// A spot light without a rectangle whose image slot (sceneLights[i].source.w)
// is N >= 1 shines through a cookie: tile N - 1 of `sampler2D lightCookies`,
// which the including shader declares, a 4 x 4 grid of tiles numbered left
// to right, top to bottom. The tile fills the square around the cone, its
// top towards the light's up axis.

#ifndef DEFAULT_LIGHTS_GLSL
#define DEFAULT_LIGHTS_GLSL

#include "area_lights.glsl"

#ifdef LOCAL_SHADOWS
#include "local_shadows.glsl"
#endif
#ifdef CLUSTERED
#include "clusters.glsl"
#endif

// Light from scene light i, shaded as its rectangle. Its radiance keeps the
// light's own falloff: far away the rectangle gives what the point light
// would have, times the cosine a flat panel's brightness falls off with
// across its face. Near it the rectangle's shape and size take over; within
// half its diagonal the falloff stops growing.
vec3 rectangleLight(uint i, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                    vec3 F0) {
    vec3 centre = sceneLights[i].positionType.xyz;
    vec3 forward = normalize(sceneLights[i].directionRange.xyz);
    float range = sceneLights[i].directionRange.w;
    vec4 attenuation = sceneLights[i].attenuation;
    vec4 shape = sceneLights[i].shape;
    vec3 toLight = centre - worldPos;
    float dist = length(toLight);
    vec3 L = toLight / max(dist, 1e-4);
    float theta = dot(-L, forward);
    if (theta <= 0.0) return vec3(0.0);     // behind the panel

    vec3 halfX = shape.xyz;
    vec3 halfY = normalize(cross(halfX, forward)) * shape.w;
    float d = max(dist, sqrt(dot(halfX, halfX) + shape.w * shape.w));
    float falloff = 1.0 / max(attenuation.x + attenuation.y * d + attenuation.z * d * d, 1e-4);
    if (range > 0.0) {
        float r = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
        falloff *= r * r;
    }
    if (sceneLights[i].positionType.w > 1.5) {    // a spot's cone
        float cosOuter = attenuation.w;
        falloff *= smoothstep(cosOuter, mix(cosOuter, 1.0, 0.1), theta);
    }
    if (falloff <= 1e-4) return vec3(0.0);

    vec3 reflected = rectangleBRDF(N, V, toLight, halfX, halfY, albedo, metallic, roughness, F0);
    if (max(reflected.r, max(reflected.g, reflected.b)) <= 0.0) return vec3(0.0);
#ifdef LOCAL_SHADOWS
    falloff *= localShadowVisibility(int(i), sceneLights[i].positionType.w, worldPos, N, L);
#endif
    vec3 color = sceneLights[i].colorIntensity.rgb * sceneLights[i].colorIntensity.w;
    return reflected * color * falloff * d * d / (4.0 * length(halfX) * shape.w);
}

// The colour spot light i's cookie lets through towards a point in
// direction -L from the light; white without one.
vec3 spotCookie(uint i, vec3 L) {
    float slot = sceneLights[i].source.w;
    if (slot < 0.5) return vec3(1.0);
    vec3 F = normalize(sceneLights[i].directionRange.xyz);
    vec3 R = sceneLights[i].shape.xyz;              // the unit right axis
    vec3 U = cross(R, F);
    vec3 D = -L;
    float z = dot(D, F);
    if (z <= 1e-4) return vec3(0.0);
    float cosOuter = clamp(sceneLights[i].attenuation.w, 1e-4, 1.0);
    float tanOuter = sqrt(max(1.0 - cosOuter * cosOuter, 1e-8)) / cosOuter;
    vec2 p = vec2(dot(D, R), dot(D, U)) / (z * tanOuter);   // -1..1 across the cone
    vec2 uv = vec2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y);
    // Inside the tile by half a texel, so filtering never reads its neighbour.
    vec2 halfTexel = 0.5 / (vec2(textureSize(lightCookies, 0)) * 0.25);
    uv = clamp(uv, halfTexel, 1.0 - halfTexel);
    int tile = clamp(int(slot + 0.5) - 1, 0, 15);
    return textureLod(lightCookies, (vec2(tile % 4, tile / 4) + uv) * 0.25, 0.0).rgb;
}

// Light from scene light i. `sunIndex` is the light shadowed by
// `sunVisibility`; -1 when none is.
vec3 lightFrom(uint i, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
               vec3 F0, int sunIndex, float sunVisibility) {
    float type = sceneLights[i].positionType.w;
    vec3 color = sceneLights[i].colorIntensity.rgb * sceneLights[i].colorIntensity.w;
    vec3 direction = sceneLights[i].directionRange.xyz;
    float range = sceneLights[i].directionRange.w;
    vec4 attenuation = sceneLights[i].attenuation;

    if (type > 0.5 && sceneLights[i].shape.w > 0.0) {
        return rectangleLight(i, worldPos, N, V, albedo, metallic, roughness, F0);
    }

    vec3 L;
    float falloff = 1.0;
    if (type < 0.5) {                       // directional
        L = -normalize(direction);
        if (int(i) == sunIndex) falloff = sunVisibility;
    } else {                                // point or spot
        vec3 toLight = sceneLights[i].positionType.xyz - worldPos;
        float dist = length(toLight);
        L = toLight / max(dist, 1e-4);
        falloff = 1.0 / max(attenuation.x + attenuation.y * dist + attenuation.z * dist * dist, 1e-4);
        if (range > 0.0) {
            // Smooth window to zero at the range.
            float r = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
            falloff *= r * r;
        }
        if (type > 1.5) {
            float cosOuter = attenuation.w;
            float theta = dot(L, -normalize(direction));
            falloff *= smoothstep(cosOuter, mix(cosOuter, 1.0, 0.1), theta);
            if (falloff > 1e-4) color *= spotCookie(i, L);
        }
#ifdef LOCAL_SHADOWS
        if (falloff > 1e-4 && dot(N, L) > 0.0) {
            falloff *= localShadowVisibility(int(i), type, worldPos, N, L);
        }
#endif
    }

    float NdotL = dot(N, L);
    if (NdotL <= 0.0 || falloff <= 1e-4) return vec3(0.0);
    return cookTorrance(N, V, L, albedo, metallic, roughness, F0) * color * NdotL * falloff;
}

#ifdef CLUSTERED
// Light from the directional lights and the lights of cluster `cluster`
// (clusters.glsl), listed by light_clusters.comp: `lightClusters` holds each
// list's count and offset, `clusterLightIndices` the lists. The including
// shader declares both.
vec3 directLight(uint cluster, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                 vec3 F0, int sunIndex, float sunVisibility) {
    vec3 total = vec3(0.0);
    for (int column = 0; column < 2; ++column) {
        int x = column == 0 ? CLUSTER_COUNT : int(cluster);
        uint count = texelFetch(lightClusters, ivec2(x, 0), 0).r;
        uint offset = texelFetch(lightClusters, ivec2(x, 1), 0).r;
        for (uint k = 0u; k < count; ++k) {
            uint i = clusterLightIndices[offset + k];
            total += lightFrom(i, worldPos, N, V, albedo, metallic, roughness, F0, sunIndex, sunVisibility);
        }
    }
    return total;
}
#else
// Light from every scene light.
vec3 directLight(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                 vec3 F0, int sunIndex, float sunVisibility) {
    vec3 total = vec3(0.0);
    for (uint i = 0u; i < SCENE_LIGHT_COUNT; ++i) {
        total += lightFrom(i, worldPos, N, V, albedo, metallic, roughness, F0, sunIndex, sunVisibility);
    }
    return total;
}
#endif

// Image-based light, diffuse irradiance and split-sum specular apart, for
// occluding them differently.
void ambientLightParts(samplerCube irradianceMap, samplerCube prefilterMap, sampler2D brdfLUT,
                       vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0,
                       out vec3 diffuse, out vec3 specular) {
    float NdotV = max(dot(N, V), 1e-4);
    vec3 kS = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    diffuse = kD * texture(irradianceMap, N).rgb * albedo;

    float maxLod = float(textureQueryLevels(prefilterMap) - 1);
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(prefilterMap, R, roughness * maxLod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    specular = prefiltered * (F0 * brdf.x + brdf.y);
}

vec3 ambientLight(samplerCube irradianceMap, samplerCube prefilterMap, sampler2D brdfLUT,
                  vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 diffuse, specular;
    ambientLightParts(irradianceMap, prefilterMap, brdfLUT, N, V, albedo, metallic, roughness, F0,
                      diffuse, specular);
    return diffuse + specular;
}

#endif
