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

#ifndef DEFAULT_LIGHTS_GLSL
#define DEFAULT_LIGHTS_GLSL

#ifdef LOCAL_SHADOWS
#include "local_shadows.glsl"
#endif
#ifdef CLUSTERED
#include "clusters.glsl"
#endif
#ifdef RT_LOCAL_SHADOWS
#include "rt_lights.glsl"
#endif

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
    float falloff = 1.0;
    if (type < 0.5) {                       // directional
        L = -normalize(direction);
        Ls = L;
        if (int(i) == sunIndex) falloff = sunVisibility;
    } else {                                // point or spot
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
    return sphereLightBRDF(N, V, L, Ls, energy, albedo, metallic, roughness, F0) * color * falloff;
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
}

vec3 ambientLight(samplerCube irradianceMap, samplerCube prefilterMap, sampler2D brdfLUT,
                  vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 diffuse, specular;
    ambientLightParts(irradianceMap, prefilterMap, brdfLUT, N, V, albedo, metallic, roughness, F0,
                      diffuse, specular);
    return diffuse + specular;
}

#endif
