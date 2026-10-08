// lights.glsl - direct light from scene.lights[i], shared by the lighting
// and forward passes. The including shader declares the Lights block
// without an instance name (DEFAULT_LIGHTS_BLOCK) and includes sk/pbr.glsl.
// With CLUSTERED defined, directLight takes the fragment's cluster and loops
// over its lights only; with LOCAL_SHADOWS, spot and point lights are
// shadowed from the atlas.
//
// The showroom's copy treats point and spot lights as spheres of
// lightsSource[i].x radius: their highlights take the sphere's size (Karis
// 2013, representative point), and with RT_LOCAL_SHADOWS defined, lights
// whose shadow flag is set (lightsSource[i].y) are shadowed by a ray to a
// random point of the sphere, which TAA averages into a soft shadow. The
// atlas then holds only what the rays cannot see (alpha-tested and skinned
// casters), and the two are multiplied. RT_HARD_LIGHT_SHADOWS aims the ray
// at the centre instead, for surfaces TAA cannot average (blended ones).
// Spot lights fade from their inner cone (lightsSource[i].z) to the outer.
//
// Lights with a rectangle (lightsShape[i]: its right axis times half its
// width, and half its height) are shaded as that rectangle by linearly
// transformed cosines (area_lights.glsl), where showroom.areaLights is on:
// their highlights take its shape, and the shadow rays aim at random points
// of it. The rectangle faces the light's direction and lights nothing
// behind it.

#ifndef DEFAULT_LIGHTS_GLSL
#define DEFAULT_LIGHTS_GLSL

// Clear coat over the base (KHR_materials_clearcoat), set by the including
// shader for the surface being lit before it calls directLight and
// ambientLight. A dielectric layer (F0 0.04) with its own roughness, sharing
// the base's normal: its specular is added, and the base beneath it receives
// what the coat's Fresnel lets through (Filament's model).
float coatFactor = 0.0;
float coatRoughness = 0.045;
// The coat's share of ambientLight's specular, for the ray-traced
// reflections to replace.
vec3 ambientCoatSpecular = vec3(0.0);

const float COAT_F0 = 0.04;
float coatFresnel(float cosTheta) {
    return COAT_F0 + (1.0 - COAT_F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

#ifdef LOCAL_SHADOWS
#include "local_shadows.glsl"
#endif
#ifdef CLUSTERED
#include "clusters.glsl"
#endif
#ifdef RT_LOCAL_SHADOWS
#include "rt_lights.glsl"
#endif
#include "area_lights.glsl"

// Diffuse towards L and specular towards Ls (the representative point of a
// sphere light), the specular lobe scaled by `energy`, each times its NdotL.
vec3 sphereLightBRDF(vec3 N, vec3 V, vec3 L, vec3 Ls, float energy, vec3 albedo, float metallic,
                     float roughness, vec3 F0) {
    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = dot(N, L);
    float NdotLs = dot(N, Ls);
    vec3 result = vec3(0.0);
    if (NdotLs > 0.0) {
        vec3 H = normalize(V + Ls);
        vec3 F = fresnelSchlick(max(dot(V, H), 0.0), F0);
        float D = distributionGGX(max(dot(N, H), 0.0), roughness);
        float G = geometrySmith(NdotV, NdotLs, roughness);
        result += (D * G * F) / max(4.0 * NdotV * NdotLs, 0.001) * energy * NdotLs;
    }
    if (NdotL > 0.0) {
        vec3 F = fresnelSchlick(max(dot(V, normalize(V + L)), 0.0), F0);
        result += (1.0 - F) * (1.0 - metallic) * albedo / SK_PI * NdotL;
    }
    return result;
}

// Light from scene light i, shaded as its rectangle. Its radiance keeps the
// light's own falloff: far away the rectangle gives what the point light
// would have, times the cosine a flat panel's brightness falls off with
// across its face. Near it the rectangle's shape and size take over; within
// half its diagonal the falloff stops growing.
vec3 rectangleLight(uint i, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                    vec3 F0) {
    vec3 centre = lightsPositionType[i].xyz;
    vec3 forward = normalize(lightsDirectionRange[i].xyz);
    float range = lightsDirectionRange[i].w;
    vec4 attenuation = lightsAttenuation[i];
    vec4 shape = lightsShape[i];
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
    if (lightsPositionType[i].w > 1.5) {    // a spot's cone: the softbox's grid
        float cosOuter = attenuation.w;
        float cosInner = max(lightsSource[i].z, cosOuter + 1e-3);
        falloff *= smoothstep(cosOuter, cosInner, theta);
    }
    if (falloff <= 1e-4) return vec3(0.0);

    vec3 reflected = rectangleBRDF(N, V, toLight, halfX, halfY, albedo, metallic, roughness, F0);
    if (max(reflected.r, max(reflected.g, reflected.b)) <= 0.0) return vec3(0.0);
#ifdef RT_LOCAL_SHADOWS
    if (lightsSource[i].y > 0.5 && localRayBlockedRect(i, worldPos, N, centre, halfX, halfY)) return vec3(0.0);
#endif
#ifdef LOCAL_SHADOWS
    falloff *= localShadowVisibility(int(i), lightsPositionType[i].w, worldPos, N, L);
#endif
    vec3 color = lightsColorIntensity[i].rgb * lightsColorIntensity[i].w;
    return reflected * color * falloff * d * d / (4.0 * length(halfX) * shape.w);
}

// Light from scene light i. `sunIndex` is the light shadowed by
// `sunVisibility`; -1 when none is.
vec3 lightFrom(uint i, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
               vec3 F0, int sunIndex, float sunVisibility) {
    float type = lightsPositionType[i].w;
    vec3 color = lightsColorIntensity[i].rgb * lightsColorIntensity[i].w;
    vec3 direction = lightsDirectionRange[i].xyz;
    float range = lightsDirectionRange[i].w;
    vec4 attenuation = lightsAttenuation[i];
    vec4 source = lightsSource[i];

    vec3 L;
    vec3 Ls;
    float energy = 1.0;
    float coatEnergy = 1.0;
    float falloff = 1.0;
    if (type < 0.5) {                       // directional
        L = -normalize(direction);
        Ls = L;
        if (int(i) == sunIndex) falloff = sunVisibility;
    } else {                                // point or spot
        if (lightsShape[i].w > 0.0 && showroom.areaLights > 0.5) {
            return rectangleLight(i, worldPos, N, V, albedo, metallic, roughness, F0);
        }
        vec3 toLight = lightsPositionType[i].xyz - worldPos;
        float dist = length(toLight);
        L = toLight / max(dist, 1e-4);
        Ls = L;
        float radius = source.x;
        if (radius > 0.0) {
            // The point of the sphere nearest the reflected ray, and the
            // lobe widened by the sphere's size, energy kept.
            vec3 R = reflect(-V, N);
            vec3 centreToRay = dot(toLight, R) * R - toLight;
            vec3 closest = toLight + centreToRay * clamp(radius / max(length(centreToRay), 1e-4), 0.0, 1.0);
            Ls = normalize(closest);
            float a = roughness * roughness;
            float widened = clamp(a + radius / (2.0 * max(dist, 1e-4)), 0.0, 1.0);
            energy = (a / widened) * (a / widened);
            // The glossier coat widens far more, relatively: a sharp
            // highlight the softbox's size.
            float ac = coatRoughness * coatRoughness;
            float widenedCoat = clamp(ac + radius / (2.0 * max(dist, 1e-4)), 0.0, 1.0);
            coatEnergy = (ac / widenedCoat) * (ac / widenedCoat);
            dist = max(dist, radius);
        }
        falloff = 1.0 / max(attenuation.x + attenuation.y * dist + attenuation.z * dist * dist, 1e-4);
        if (range > 0.0) {
            // Smooth window to zero at the range.
            float r = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
            falloff *= r * r;
        }
        if (type > 1.5) {
            float cosOuter = attenuation.w;
            float cosInner = max(source.z, cosOuter + 1e-3);
            float theta = dot(L, -normalize(direction));
            falloff *= smoothstep(cosOuter, cosInner, theta);
        }
        bool facing = dot(N, L) > 0.0 || dot(N, Ls) > 0.0;
#ifdef RT_LOCAL_SHADOWS
        if (falloff > 1e-4 && facing && source.y > 0.5) {
            if (localRayBlocked(i, worldPos, N, lightsPositionType[i].xyz, radius)) return vec3(0.0);
        }
#endif
#ifdef LOCAL_SHADOWS
        if (falloff > 1e-4 && facing) {
            falloff *= localShadowVisibility(int(i), type, worldPos, N, L);
        }
#endif
    }

    if (falloff <= 1e-4) return vec3(0.0);
    vec3 brdf = sphereLightBRDF(N, V, L, Ls, energy, albedo, metallic, roughness, F0);
    float NdotLs = dot(N, Ls);
    if (coatFactor > 0.0 && NdotLs > 0.0) {
        float NdotV = max(dot(N, V), 1e-4);
        vec3 H = normalize(V + Ls);
        float Fc = coatFresnel(max(dot(V, H), 0.0)) * coatFactor;
        float D = distributionGGX(max(dot(N, H), 0.0), coatRoughness);
        float G = geometrySmith(NdotV, NdotLs, coatRoughness);
        float coat = D * G * Fc / max(4.0 * NdotV * NdotLs, 0.001) * coatEnergy * NdotLs;
        brdf = brdf * (1.0 - Fc) + vec3(coat);
    }
    return brdf * color * falloff;
}

#ifdef CLUSTERED
// Light from the directional lights and the lights of cluster `cluster`
// (clusters.glsl), listed in `lightClusters` by light_clusters.comp. The
// including shader declares `usampler2D lightClusters`.
vec3 directLight(uint cluster, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                 vec3 F0, int sunIndex, float sunVisibility) {
    vec3 total = vec3(0.0);
    for (int column = 0; column < 2; ++column) {
        int x = column == 0 ? CLUSTER_COUNT : int(cluster);
        uint count = texelFetch(lightClusters, ivec2(x, 0), 0).r;
        for (uint k = 0u; k < count; ++k) {
            uint i = texelFetch(lightClusters, ivec2(x, int(k) + 1), 0).r;
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
    for (uint i = 0u; i < min(lightCount, uint(MAX_LIGHTS)); ++i) {
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

    ambientCoatSpecular = vec3(0.0);
    if (coatFactor > 0.0) {
        float Fc = coatFresnel(NdotV) * coatFactor;
        diffuse *= 1.0 - Fc;
        specular *= 1.0 - Fc;
        vec3 coatPrefiltered = textureLod(prefilterMap, R, coatRoughness * maxLod).rgb;
        vec2 coatBrdf = texture(brdfLUT, vec2(NdotV, coatRoughness)).rg;
        ambientCoatSpecular = coatPrefiltered * (COAT_F0 * coatBrdf.x + coatBrdf.y) * coatFactor;
        specular += ambientCoatSpecular;
    }
}

vec3 ambientLight(samplerCube irradianceMap, samplerCube prefilterMap, sampler2D brdfLUT,
                  vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 diffuse, specular;
    ambientLightParts(irradianceMap, prefilterMap, brdfLUT, N, V, albedo, metallic, roughness, F0,
                      diffuse, specular);
    return diffuse + specular;
}

#endif
