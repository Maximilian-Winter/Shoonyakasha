// forward_body.glsl - forward shading of blended materials (glTF alphaMode
// BLEND) and transmissive ones (KHR_materials_transmission), over the lit
// opaque scene, fogged as pass HeightFog fogs that (fog.glsl). Shadows come
// from the cascades directly: the shadow mask only covers the opaque surfaces
// behind. A transmissive surface lets through the opaque scene behind it
// (sceneBehind), in place of its diffuse; it is drawn straight into the HDR
// target, with OIT too, at its alpha for BLEND and fully for the other modes.
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

layout(set = 1, binding = 7) uniform sampler2D transmissionMap;   // KHR_materials_transmission: r amount
layout(set = 1, binding = 8) uniform sampler2D thicknessMap;      // KHR_materials_volume: g thickness
#ifdef RAY_TRACED_SHADOWS
#define TRANSMISSION_SET 8
#else
#define TRANSMISSION_SET 7
#endif
// The lit, fogged opaque scene, with mips: what a transmissive surface lets
// through (pass SceneBehind).
layout(set = TRANSMISSION_SET, binding = 0) uniform sampler2D sceneBehind;

// Light let through a transmissive surface: the scene behind, where the ray
// refracted at the surface leaves a volume `thickness` deep (straight behind
// for a thin surface), blurred more for rougher surfaces, tinted by the base
// colour and by the volume's absorption over that distance.
vec3 transmittedLight(vec3 N, vec3 V, vec3 baseColor, float roughness, float ior, float thickness) {
    vec3 refracted = refract(-V, N, 1.0 / ior);
    vec3 exitPoint = fragWorldPos + refracted * thickness;
    vec4 clip = camera.proj * camera.view * vec4(exitPoint, 1.0);
    vec2 uv = clip.w > 0.0 ? clamp(clip.xy / clip.w * 0.5 + 0.5, 0.0, 1.0) : gl_FragCoord.xy / camera.resolution;
    // Roughness counts less the closer ior is to 1 (glTF sample viewer).
    float blur = roughness * clamp(ior * 2.0 - 2.0, 0.0, 1.0);
    float lod = min(log2(camera.resolution.x) * blur, float(textureQueryLevels(sceneBehind) - 1));
    vec3 behind = textureLod(sceneBehind, uv, lod).rgb;
    vec3 absorbed = vec3(1.0);
    if (draw.attenuationDistance > 0.0 && thickness > 0.0) {
        absorbed = pow(max(draw.attenuationColor.rgb, vec3(1e-4)), vec3(thickness / draw.attenuationDistance));
    }
    return behind * baseColor * absorbed;
}

layout(location = 0) out vec4 outColor;      // alpha-blended over the HDR target
// With settings.oit, weighted blended order-independent transparency
// (McGuire and Bavoil 2013) instead: outColor leaves the HDR target as it is,
// transparentAccum adds the surface's premultiplied colour and coverage times
// a weight that falls with distance, transparentReveal multiplies by what the
// surface lets through, and pass TransparentComposite (transparent_composite
// .frag) blends the weighted average over the scene.
layout(location = 1) out vec4 outAccum;
layout(location = 2) out vec4 outReveal;

// How much of sun s (0 or 1) reaches the fragment: its cascades, and with
// RAY_TRACED_SHADOWS rays towards its disc.
float sunReaching(int s, vec3 N, float viewDepth, float noise) {
    vec3 L = -normalize(sunTravel(s));
    int cascade;
    float lit = sunVisibility(s, fragWorldPos, N, L, viewDepth, noise, cascade);
#ifdef RAY_TRACED_SHADOWS
    if (sunCascadeCount(s) > 0 && lit > 0.0) {
        // Off whichever side faces the sun: blended surfaces are often thin
        // and seen from both sides.
        vec3 origin = fragWorldPos + (dot(N, L) < 0.0 ? -N : N) * sunRayOffset(viewDepth);
        int open = 0;
        for (int r = 0; r < SUN_RAYS; ++r) {
            float u = fract(noise + float(r) / float(SUN_RAYS));
            float v = temporalNoise(gl_FragCoord.yx + 17.0 * float(r + 1) + 31.0 * float(s), camera.frame + 3u,
                                    camera.taa);
            if (!sunRayBlocked(origin, L, u, v)) ++open;
        }
        lit *= float(open) / float(SUN_RAYS);
    }
#endif
    return lit;
}

void main() {
    Surface s = evaluateSurface(surfaceBaseColor());
    if (draw.alphaMode == 1u && s.baseColor.a < draw.alphaCutoff) discard;
    coatFactor = s.clearcoat;
    coatRoughness = s.clearcoatRoughness;
    vec3 V = normalize(camera.position.xyz - fragWorldPos);
    sheenColor = s.sheenColor;
    sheenRoughness = s.sheenRoughness;
    float ior = max(draw.ior, 1.0);
    vec3 F0 = mix(s.dielectricF0, s.baseColor.rgb, s.metallic);
    specularF90 = mix(s.specularWeight, 1.0, s.metallic);
    setFilm(s.iridescence, s.filmIor, s.filmThickness, max(dot(s.N, V), 1e-4), F0);
    float transmission = clamp(draw.transmissionFactor, 0.0, 1.0);
    if (transmission > 0.0) transmission *= texture(transmissionMap, MATERIAL_UV(transmission)).r;
    // The transmitted part of the dielectric takes the place of its diffuse.
    vec3 diffuseAlbedo = s.baseColor.rgb * (1.0 - transmission);

    float viewDepth = -(camera.view * vec4(fragWorldPos, 1.0)).z;
    float noise = temporalNoise(gl_FragCoord.xy, camera.frame, camera.taa);
    float sunLit = sunReaching(0, s.N, viewDepth, noise);
    secondSunIndex = sunLight(1);
    secondSunVisibility = secondSunIndex >= 0 ? sunReaching(1, s.N, viewDepth, noise) : 1.0;
#ifdef RAY_TRACED_SHADOWS
    rtLightPixel = gl_FragCoord.xy;
    rtLightOffset = sunRayOffset(viewDepth);
#endif
    int sunIndex = sunLight(0);

    uint cluster = clusterIndex(gl_FragCoord.xy / camera.resolution, viewDepth, camera.params.x, camera.params.y);
    vec3 color = directLight(cluster, fragWorldPos, s.N, V, diffuseAlbedo, s.metallic, s.roughness, F0,
                             sunIndex, sunLit)
               + ambientLight(irradianceMap, prefilterMap, brdfLUT, s.N, V, diffuseAlbedo,
                              s.metallic, s.roughness, F0)
                 * settings.iblIntensity * s.occlusion * mix(settings.shadowAmbient, 1.0, sunLit)
               + s.emissive;
    if (transmission > 0.0) {
        // Thickness is in mesh units: scaled by the model matrix.
        float thickness = draw.thicknessFactor * texture(thicknessMap, MATERIAL_UV(thickness)).g;
        thickness *= (length(draw.model[0].xyz) + length(draw.model[1].xyz) + length(draw.model[2].xyz)) / 3.0;
        float NdotV = max(dot(s.N, V), 1e-4);
        vec2 brdf = texture(brdfLUT, vec2(NdotV, s.roughness)).rg;
        vec3 reflected = splitSumAlbedo(F0, brdf);
        color += (1.0 - reflected) * transmission * (1.0 - s.metallic)
               * transmittedLight(s.N, V, s.baseColor.rgb, s.roughness, ior, thickness);
    }
    if (settings.debugView == 0u) {
        vec3 toSurface = fragWorldPos - camera.position.xyz;
        float distance = length(toSurface);
        vec3 dir = toSurface / max(distance, 1e-4);
        color = mix(color, fogLight(dir), fogAmount(camera.position.xyz, dir, distance));
    }
    float alpha = draw.alphaMode == 2u ? s.baseColor.a : 1.0;
    if (settings.oit != 0u && transmission <= 0.0) {
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
