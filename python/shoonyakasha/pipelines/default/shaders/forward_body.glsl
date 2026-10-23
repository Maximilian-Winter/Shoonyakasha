// forward_body.glsl - forward shading of blended materials (glTF alphaMode
// BLEND), over the lit opaque scene, fogged as pass HeightFog fogs that
// (fog.glsl). Shadows come from the cascades directly: the shadow mask only
// covers the opaque surfaces behind.
// forward_rt.frag builds it with RAY_TRACED_SHADOWS, for the "raytraced"
// preset, where the cascades hold only the casters the rays do not see and
// spot and point lights are shadowed by rays (RT_LOCAL_SHADOWS) instead of
// the shadow atlas.

#include "common.glsl"
#include "sk/pbr.glsl"
#include "material_draw.glsl"
#define EXPOSURE_SET 6
#include "surface.glsl"

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Lights { DEFAULT_LIGHTS_BLOCK };
DEFAULT_SCENE_LIGHTS(2, 4)
layout(set = 2, binding = 1) uniform LocalShadowMatrices { DEFAULT_LOCAL_SHADOWS_BLOCK } localShadows;
layout(set = 2, binding = 2) uniform sampler2DShadow localShadowAtlas;
layout(set = 2, binding = 3) uniform usampler2D lightClusters;
layout(set = 2, binding = 5, std430) readonly buffer ClusterLightIndices { uint clusterLightIndices[]; };
layout(set = 2, binding = 6, std430) readonly buffer LtcTable { vec4 ltcTable[]; };
layout(set = 2, binding = 7) uniform sampler2D lightCookies;
layout(set = 2, binding = 8) uniform sampler2D lightProfiles;
layout(set = 3, binding = 0) uniform samplerCube irradianceMap;
layout(set = 3, binding = 1) uniform samplerCube prefilterMap;
layout(set = 3, binding = 2) uniform sampler2D brdfLUT;
layout(set = 4, binding = 0) uniform sampler2DArrayShadow shadowMap;
layout(set = 5, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 5, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;

#include "csm.glsl"
#ifdef RAY_TRACED_SHADOWS
layout(set = 7, binding = 0) uniform accelerationStructureEXT scene;
#define SK_SCENE_HITS_SET 7
#define SK_RAY_INSTANCES_BINDING 1
#define SK_SCENE_TEXTURES_BINDING 2
#include "sk/scene_hits.glsl"
#include "rt_shadows.glsl"
const int SUN_RAYS = 4;   // more than the opaque pass: TAA cannot follow a blended surface
#define RT_LOCAL_SHADOWS
#else
#define LOCAL_SHADOWS
#endif
#define CLUSTERED
#include "lights.glsl"
#include "fog.glsl"

layout(location = 0) out vec4 outColor;      // alpha-blended over the HDR target
// With settings.oit, weighted blended order-independent transparency
// (McGuire and Bavoil 2013) instead: outColor leaves the HDR target as it is,
// transparentAccum adds the surface's premultiplied colour and coverage times
// a weight that falls with distance, transparentReveal multiplies by what the
// surface lets through, and pass TransparentComposite (transparent_composite
// .frag) blends the weighted average over the scene.
layout(location = 1) out vec4 outAccum;
layout(location = 2) out vec4 outReveal;

void main() {
    Surface s = evaluateSurface(surfaceBaseColor());
    coatFactor = s.clearcoat;
    coatRoughness = s.clearcoatRoughness;
    vec3 V = normalize(camera.position.xyz - fragWorldPos);
    vec3 F0 = baseReflectivity(s.baseColor.rgb, s.metallic);

    float viewDepth = -(camera.view * vec4(fragWorldPos, 1.0)).z;
    vec3 L = -normalize(cascades.sunDirection.xyz);
    int cascade;
    float noise = temporalNoise(gl_FragCoord.xy, camera.frame, camera.taa);
    float sunLit = sunVisibility(fragWorldPos, s.N, L, viewDepth, noise, cascade);
#ifdef RAY_TRACED_SHADOWS
    if (cascades.sunEnabled != 0u && sunLit > 0.0) {
        // Off whichever side faces the sun: blended surfaces are often thin
        // and seen from both sides.
        vec3 origin = fragWorldPos + (dot(s.N, L) < 0.0 ? -s.N : s.N) * sunRayOffset(viewDepth);
        int open = 0;
        for (int r = 0; r < SUN_RAYS; ++r) {
            float u = fract(noise + float(r) / float(SUN_RAYS));
            float v = temporalNoise(gl_FragCoord.yx + 17.0 * float(r + 1), camera.frame + 3u, camera.taa);
            if (!sunRayBlocked(origin, L, u, v)) ++open;
        }
        sunLit *= float(open) / float(SUN_RAYS);
    }
    rtLightPixel = gl_FragCoord.xy;
    rtLightOffset = sunRayOffset(viewDepth);
#endif
    int sunIndex = cascades.sunEnabled != 0u ? cascades.sunLightIndex : -1;

    uint cluster = clusterIndex(gl_FragCoord.xy / camera.resolution, viewDepth, camera.params.x, camera.params.y);
    vec3 color = directLight(cluster, fragWorldPos, s.N, V, s.baseColor.rgb, s.metallic, s.roughness, F0,
                             sunIndex, sunLit)
               + ambientLight(irradianceMap, prefilterMap, brdfLUT, s.N, V, s.baseColor.rgb,
                              s.metallic, s.roughness, F0)
                 * settings.iblIntensity * s.occlusion * mix(settings.shadowAmbient, 1.0, sunLit)
               + s.emissive;
    if (settings.debugView == 0u) {
        vec3 toSurface = fragWorldPos - camera.position.xyz;
        float distance = length(toSurface);
        vec3 dir = toSurface / max(distance, 1e-4);
        color = mix(color, fogLight(dir), fogAmount(camera.position.xyz, dir, distance));
    }
    float alpha = s.baseColor.a;
    if (settings.oit != 0u) {
        // McGuire and Bavoil's equation 10: nearer surfaces weigh more.
        float weight = alpha * clamp(10.0 / (1e-5 + pow(viewDepth / 10.0, 3.0) + pow(viewDepth / 200.0, 6.0)),
                                     1e-2, 3e3);
        outColor = vec4(0.0);
        outAccum = vec4(color * alpha, alpha) * weight;
        outReveal = vec4(alpha);
    } else {
        outColor = vec4(color, alpha);
        outAccum = vec4(0.0);
        outReveal = vec4(0.0);
    }
}
