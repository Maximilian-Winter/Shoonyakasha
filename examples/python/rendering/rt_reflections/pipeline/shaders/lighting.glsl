// lighting.glsl - light at a surface a ray hit: the scene lights through
// shadow rays, and the sky. The scene lights (set 2), the acceleration
// structure with the surfaces' rows and textures (set 3) and the
// environment's maps (set 4) are declared here; the including shader
// enables GL_EXT_ray_query and includes common.glsl and ircache.glsl first.

#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

#include "sk/pbr.glsl"

struct SceneLight {
    vec4 positionType;
    vec4 colorIntensity;
    vec4 directionRange;
    vec4 attenuation;
    vec4 source;
    vec4 shape;
};
layout(set = 2, binding = 0) uniform Lights { uint lightCount; } lights;
layout(set = 2, binding = 1, std430) readonly buffer SceneLights { SceneLight sceneLights[]; };

layout(set = 3, binding = 0) uniform accelerationStructureEXT scene;
#define SK_SCENE_HITS_SET 3
#define SK_RAY_INSTANCES_BINDING 1
#define SK_SCENE_TEXTURES_BINDING 2
#include "sk/scene_hits.glsl"

layout(set = 4, binding = 1) uniform samplerCube prefilterMap;

const float RAY_FAR = 10000.0;

// An orthonormal basis around n (Duff et al. 2017).
void basis(vec3 n, out vec3 t, out vec3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}

bool visible(vec3 origin, vec3 direction, float distance) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                          0xFF, origin, 0.0, direction, distance);
    while (rayQueryProceedEXT(query)) {}
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT;
}

// Light from scene light i reflected towards V: the default pipeline's light
// model (lights.glsl), with a shadow ray. Directional lights are discs of
// angular radius settings.sunAngle and lights with a source radius spheres,
// a point of which u1 and u2 pick.
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

// Light from every scene light; `rng` picks the points of area lights.
vec3 directLight(vec3 P, vec3 N, vec3 Ng, vec3 V, vec3 albedo, float metallic, float roughness, inout uint rng) {
    vec3 total = vec3(0.0);
    uint count = min(lights.lightCount, uint(sceneLights.length()));
    for (uint i = 0u; i < count; ++i) {
        float u1 = uintToU01(hash1Mut(rng)), u2 = uintToU01(hash1Mut(rng));
        total += lightFrom(i, P, N, Ng, V, albedo, metallic, roughness, u1, u2);
    }
    return total;
}

vec3 environment(vec3 direction) {
    return textureLod(prefilterMap, direction, 0.0).rgb * settings.environmentIntensity;
}

#endif
