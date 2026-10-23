// gi_lighting.glsl - light at a surface an irradiance cache ray hit: the
// shadowed suns, two of the other scene lights picked at random (counted for
// all of them), each through a shadow ray, and the sky past the scene.
// Declares the scene lights (set 3, "lightsComputeSet"), the acceleration
// structure with the surfaces' rows and textures (set 4,
// "rayTracingSceneSet") and the environment's maps (set 5, "iblSet"). The
// including shader enables GL_EXT_ray_query and includes gi_common.glsl
// first.

#ifndef DEFAULT_GI_LIGHTING_GLSL
#define DEFAULT_GI_LIGHTING_GLSL

#include "sk/pbr.glsl"

layout(set = 3, binding = 0) uniform Lights { DEFAULT_LIGHTS_BLOCK };
DEFAULT_SCENE_LIGHTS(3, 1)

layout(set = 4, binding = 0) uniform accelerationStructureEXT scene;
#define SK_SCENE_HITS_SET 4
#define SK_RAY_INSTANCES_BINDING 1
#define SK_SCENE_TEXTURES_BINDING 2
#include "sk/scene_hits.glsl"

layout(set = 5, binding = 1) uniform samplerCube prefilterMap;

const float RAY_FAR = 10000.0;
// Other lights shaded at each hit.
const uint GI_LIGHT_SAMPLES = 2u;

// An orthonormal basis around n (Duff et al. 2017).
void basis(vec3 n, out vec3 t, out vec3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}

// Whether nothing lies between `origin` and `distance` along `direction`;
// alpha-tested surfaces block where their texture's alpha passes.
bool visible(vec3 origin, vec3 direction, float distance) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.0, direction, distance);
    while (rayQueryProceedEXT(query)) {
        if (skAlphaTestCandidate(query)) rayQueryConfirmIntersectionEXT(query);
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT;
}

// The nearest surface along a ray up to `tMax`, committed in `query`,
// alpha-tested surfaces counting where their alpha passes. False when the
// ray reaches nothing.
bool traceSurface(rayQueryEXT query, vec3 origin, vec3 direction, float tMax) {
    rayQueryInitializeEXT(query, scene, gl_RayFlagsNoneEXT, 0xFF, origin, 0.0, direction, tMax);
    while (rayQueryProceedEXT(query)) {
        if (skAlphaTestCandidate(query)) rayQueryConfirmIntersectionEXT(query);
    }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

// Light from scene light i reflected towards V: the default pipeline's light
// model (lights.glsl) with a shadow ray. Directional lights are discs of
// angular radius settings.sunAngle and lights with a source radius spheres,
// a point of which u1 and u2 pick; a rectangle light shines from its centre.
vec3 lightFrom(uint i, vec3 P, vec3 N, vec3 Ng, vec3 V, vec3 albedo, float metallic, float roughness,
               float u1, float u2) {
    SceneLight light = sceneLights[i];
    float type = light.positionType.w;
    vec3 color = light.colorIntensity.rgb * light.colorIntensity.w;
    vec3 direction = light.directionRange.xyz;
    float range = light.directionRange.w;
    vec4 attenuation = light.attenuation;

    vec3 L;
    float distance;
    float falloff = 1.0;
    if (type < 0.5) {
        vec3 toSun = -normalize(direction);
        float radius = tan(radians(settings.sunAngle)) * sqrt(u1);
        float angle = 2.0 * SK_PI * u2;
        vec3 t, b;
        basis(toSun, t, b);
        L = normalize(toSun + (t * cos(angle) + b * sin(angle)) * radius);
        distance = RAY_FAR;
    } else {
        vec3 center = light.positionType.xyz;
        vec3 target = center;
        if (light.source.x > 0.0) {
            vec3 t, b;
            basis(normalize(P - center), t, b);
            float r = light.source.x * sqrt(u1), angle = 2.0 * SK_PI * u2;
            target = center + (t * cos(angle) + b * sin(angle)) * r;
        }
        vec3 toLight = target - P;
        float centerDistance = length(center - P);
        distance = length(toLight);
        L = toLight / max(distance, 1e-4);
        falloff = 1.0 / max(attenuation.x + attenuation.y * centerDistance +
                            attenuation.z * centerDistance * centerDistance, 1e-4);
        if (range > 0.0) {
            float r = clamp(1.0 - pow(centerDistance / range, 4.0), 0.0, 1.0);
            falloff *= r * r;
        }
        if (type > 1.5) {
            float cosOuter = attenuation.w;
            float theta = dot(normalize(center - P), -normalize(direction));
            falloff *= smoothstep(cosOuter, mix(cosOuter, 1.0, 0.1), theta);
        }
    }

    float NdotL = dot(N, L);
    if (NdotL <= 0.0 || dot(Ng, L) <= 0.0 || falloff <= 1e-4) return vec3(0.0);
    vec3 F0 = baseReflectivity(albedo, metallic);
    vec3 contribution = cookTorrance(N, V, L, albedo, metallic, roughness, F0) * color * NdotL * falloff;
    if (dot(contribution, contribution) <= 0.0) return vec3(0.0);
    return visible(P + Ng * 1e-3, L, max(distance - 2e-3, 1e-3)) ? contribution : vec3(0.0);
}

// Direct light at a hit: the shadowed suns always, and GI_LIGHT_SAMPLES of
// the other lights picked uniformly, each weighted by the count of lights
// over the samples. `rng` picks them and their points.
vec3 directLight(vec3 P, vec3 N, vec3 Ng, vec3 V, vec3 albedo, float metallic, float roughness, inout uint rng) {
    int sun0 = cascades.sunEnabled != 0u ? cascades.sunLightIndex : -1;
    int sun1 = cascades.secondSunEnabled != 0u ? cascades.secondSunLightIndex : -1;
    uint count = SCENE_LIGHT_COUNT;
    vec3 total = vec3(0.0);
    for (int s = 0; s < 2; ++s) {
        int index = s == 0 ? sun0 : sun1;
        if (index < 0 || uint(index) >= count) continue;
        float u1 = uintToU01(hash1Mut(rng)), u2 = uintToU01(hash1Mut(rng));
        total += lightFrom(uint(index), P, N, Ng, V, albedo, metallic, roughness, u1, u2);
    }
    uint others = count - uint(sun0 >= 0) - uint(sun1 >= 0);
    if (others == 0u) return total;
    for (uint k = 0u; k < GI_LIGHT_SAMPLES; ++k) {
        uint i = min(uint(uintToU01(hash1Mut(rng)) * float(count)), count - 1u);
        if (int(i) == sun0 || int(i) == sun1) continue;
        float u1 = uintToU01(hash1Mut(rng)), u2 = uintToU01(hash1Mut(rng));
        total += lightFrom(i, P, N, Ng, V, albedo, metallic, roughness, u1, u2) * (float(count) / float(GI_LIGHT_SAMPLES));
    }
    return total;
}

vec3 environment(vec3 direction) {
    return textureLod(prefilterMap, direction, 0.0).rgb * settings.iblIntensity;
}

#endif
