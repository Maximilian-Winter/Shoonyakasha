// forward_body.glsl - forward shading of blended materials (glTF alphaMode
// BLEND), over the lit opaque scene. Sun shadows come from the virtual
// shadow map or the cascades directly (showroom.sunShadowMode): the shadow
// mask only covers the opaque surfaces behind. forward_rt.frag builds it
// with RAY_TRACED_SHADOWS, for the "raytraced" preset, where the maps hold
// only the casters the rays do not see; it also traces a ray to each
// shadowed spot and point light (to its centre: TAA cannot average a
// surface it sees through).

#include "common.glsl"
#include "sk/pbr.glsl"
#include "material_draw.glsl"
#include "surface.glsl"

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Lights { DEFAULT_LIGHTS_BLOCK };
layout(set = 2, binding = 1) uniform LocalShadowMatrices { DEFAULT_LOCAL_SHADOWS_BLOCK } localShadows;
layout(set = 2, binding = 2) uniform sampler2DShadow localShadowAtlas;
layout(set = 2, binding = 3) uniform usampler2D lightClusters;
layout(set = 3, binding = 0) uniform samplerCube irradianceMap;
layout(set = 3, binding = 1) uniform samplerCube prefilterMap;
layout(set = 3, binding = 2) uniform sampler2D brdfLUT;
layout(set = 4, binding = 0) uniform sampler2DArrayShadow shadowMap;
layout(set = 5, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 5, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;
layout(set = 5, binding = 2) uniform Showroom { SHOWROOM_BLOCK } showroom;
layout(set = 6, binding = 0, r32ui) uniform readonly uimage2D vsmPageTable;
layout(set = 6, binding = 2, r32ui) uniform readonly uimage2D vsmPhysical;

#include "csm.glsl"
#include "vsm_sample.glsl"
#ifdef RAY_TRACED_SHADOWS
layout(set = 7, binding = 0) uniform accelerationStructureEXT scene;
#include "rt_shadows.glsl"
const int SUN_RAYS = 4;   // more than the opaque pass: TAA cannot follow a blended surface
#define RT_LOCAL_SHADOWS
#define RT_HARD_LIGHT_SHADOWS
#endif
#define LOCAL_SHADOWS
#define CLUSTERED
#include "lights.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    Surface s = evaluateSurface(surfaceBaseColor());
    vec3 V = normalize(camera.position.xyz - fragWorldPos);
    vec3 F0 = baseReflectivity(s.baseColor.rgb, s.metallic);

    float viewDepth = -(camera.view * vec4(fragWorldPos, 1.0)).z;
    vec3 L = -normalize(cascades.sunDirection.xyz);
    float noise = temporalNoise(gl_FragCoord.xy, camera.frame, camera.taa);
    float sunLit;
    if (showroom.sunShadowMode == 0u) {
        int cascade;
        sunLit = sunVisibility(fragWorldPos, s.N, L, viewDepth, noise, cascade);
    } else {
        int level;
        bool drawn;
        float pixelWorld = vsmPixelWorld(viewDepth, camera.proj, camera.resolution.y);
        sunLit = vsmVisibility(fragWorldPos, s.N, pixelWorld, noise, level, drawn);
    }
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
#endif
    int sunIndex = cascades.sunEnabled != 0u ? cascades.sunLightIndex : -1;
#ifdef RT_LOCAL_SHADOWS
    rtLightPixel = gl_FragCoord.xy;
    rtLightOffset = 0.01 + 0.002 * viewDepth;
#endif

    uint cluster = clusterIndex(gl_FragCoord.xy / camera.resolution, viewDepth, camera.params.x, camera.params.y);
    vec3 color = directLight(cluster, fragWorldPos, s.N, V, s.baseColor.rgb, s.metallic, s.roughness, F0,
                             sunIndex, sunLit)
               + ambientLight(irradianceMap, prefilterMap, brdfLUT, s.N, V, s.baseColor.rgb,
                              s.metallic, s.roughness, F0)
                 * settings.iblIntensity * s.occlusion * mix(settings.shadowAmbient, 1.0, sunLit)
               + s.emissive;
    outColor = vec4(color, s.baseColor.a);
}
