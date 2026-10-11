// forward_body.glsl - forward shading, the last step of Volume Tiled Forward
// Shading: the material, the scene's own lights (the sun and up to 128
// more), the VTFS lights listed for the fragment's cluster, image-based
// light and emission.
//
// Built with OPAQUE (forward_opaque.frag, forward_opaque_masked.frag) it
// shades opaque and alpha-tested surfaces, drawn with depth test EQUAL
// against the depth prepass so each pixel is shaded once; the sun's
// shadow and the ambient occlusion come from the screen-space passes that
// read the prepass. Without it (forward.frag, forward_rt.frag) it shades
// blended surfaces over them, with shadows from the cascades directly:
// the screen-space passes only see the opaque surfaces behind.
// forward_rt.frag builds it with RAY_TRACED_SHADOWS, for the "raytraced"
// preset, where the cascades hold only the casters the rays do not see.

#include "common.glsl"
#include "sk/pbr.glsl"
#include "material_draw.glsl"
#define EXPOSURE_SET 6
#include "surface.glsl"
#include "vtfs.glsl"

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Lights { DEFAULT_LIGHTS_BLOCK };
layout(set = 2, binding = 1) uniform LocalShadowMatrices { DEFAULT_LOCAL_SHADOWS_BLOCK } localShadows;
layout(set = 2, binding = 2) uniform sampler2DShadow localShadowAtlas;
layout(set = 3, binding = 0) uniform samplerCube irradianceMap;
layout(set = 3, binding = 1) uniform samplerCube prefilterMap;
layout(set = 3, binding = 2) uniform sampler2D brdfLUT;
#ifdef OPAQUE
layout(set = 4, binding = 0) uniform sampler2D shadowMask;
layout(set = 4, binding = 1) uniform sampler2D screenAO;    // GTAO, denoised
layout(set = 4, binding = 2) uniform sampler2D gVelocity;
#else
layout(set = 4, binding = 0) uniform sampler2DArrayShadow shadowMap;
#endif
layout(set = 5, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 5, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;

#define VTFS_SET 7
#include "vtfs_lights.glsl"

#ifndef OPAQUE
#include "csm.glsl"
#ifdef RAY_TRACED_SHADOWS
layout(set = 8, binding = 0) uniform accelerationStructureEXT scene;
#include "rt_shadows.glsl"
const int SUN_RAYS = 4;   // more than the opaque pass: TAA cannot follow a blended surface
#endif
#endif
#define LOCAL_SHADOWS
#include "lights.glsl"

#ifdef OPAQUE
layout(early_fragment_tests) in;
#endif

layout(location = 0) out vec4 outColor;

const vec3 CASCADE_COLORS[5] = vec3[](vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3), vec3(0.3, 0.5, 1.0),
                                      vec3(1.0, 1.0, 0.3), vec3(1.0));

void main() {
    vec4 baseColor = surfaceBaseColor();
#ifdef ALPHA_TEST
    if (baseColor.a < draw.alphaCutoff) discard;
#endif
    Surface s = evaluateSurface(baseColor);
    vec3 albedo = s.baseColor.rgb;
    vec3 V = normalize(camera.position.xyz - fragWorldPos);
    vec3 F0 = baseReflectivity(albedo, s.metallic);
    int sunIndex = cascades.sunEnabled != 0u ? cascades.sunLightIndex : -1;

#ifdef OPAQUE
    vec2 uv = gl_FragCoord.xy / camera.resolution;
    vec2 mask = textureLod(shadowMask, uv, 0.0).rg;
    float sunLit = mask.r;
    // Screen-space and material occlusion, with the light that bounces
    // between occluders added back for bright albedo (Jimenez 2016), and
    // occlusion of reflections from it (Lagarde 2014).
    float ao = min(textureLod(screenAO, uv, 0.0).r, s.occlusion);
    vec3 a = 2.0404 * albedo - 0.3324, b = -4.7951 * albedo + 0.6417, c = 2.7552 * albedo + 0.6903;
    vec3 diffuseAO = max(vec3(ao), ((ao * a + b) * ao + c) * ao);
    float NdotV = max(dot(s.N, V), 1e-4);
    float specularAO = clamp(pow(NdotV + ao, exp2(-16.0 * s.roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
#else
    float viewDepth = -(camera.view * vec4(fragWorldPos, 1.0)).z;
    vec3 sunL = -normalize(cascades.sunDirection.xyz);
    int cascade;
    float noise = temporalNoise(gl_FragCoord.xy, camera.frame, camera.taa);
    float sunLit = sunVisibility(fragWorldPos, s.N, sunL, viewDepth, noise, cascade);
#ifdef RAY_TRACED_SHADOWS
    if (cascades.sunEnabled != 0u && sunLit > 0.0) {
        // Off whichever side faces the sun: blended surfaces are often thin
        // and seen from both sides.
        vec3 origin = fragWorldPos + (dot(s.N, sunL) < 0.0 ? -s.N : s.N) * sunRayOffset(viewDepth);
        int open = 0;
        for (int r = 0; r < SUN_RAYS; ++r) {
            float u = fract(noise + float(r) / float(SUN_RAYS));
            float v = temporalNoise(gl_FragCoord.yx + 17.0 * float(r + 1), camera.frame + 3u, camera.taa);
            if (!sunRayBlocked(origin, sunL, u, v)) ++open;
        }
        sunLit *= float(open) / float(SUN_RAYS);
    }
#endif
    vec3 diffuseAO = vec3(s.occlusion);
    float specularAO = s.occlusion;
#endif

    Grid grid = vtfsGrid(camera.proj, camera.params.x, camera.params.y, camera.resolution);
    uint cluster = vtfsCluster(grid, camera.invProj, uvec2(gl_FragCoord.xy), gl_FragCoord.z);

    uint view = settings.debugView;
    vec3 direct = directLight(fragWorldPos, s.N, V, albedo, s.metallic, s.roughness, F0, sunIndex, sunLit)
                + (view == 8u ? vtfsDirectLightEvery(fragWorldPos, s.N, V, albedo, s.metallic, s.roughness, F0)
                              : vtfsDirectLight(cluster, fragWorldPos, s.N, V, albedo, s.metallic, s.roughness, F0));
    vec3 ambientDiffuse, ambientSpecular;
    ambientLightParts(irradianceMap, prefilterMap, brdfLUT, s.N, V, albedo, s.metallic, s.roughness, F0,
                      ambientDiffuse, ambientSpecular);
    vec3 ambient = (ambientDiffuse * diffuseAO + ambientSpecular * specularAO)
                 * settings.iblIntensity * mix(settings.shadowAmbient, 1.0, sunLit);
    vec3 color = direct + ambient + s.emissive;

    // Debug views (README.md); the blended surfaces show the VTFS ones only.
    // View 8, every light without clusters, is the image above.
    if (view == 5u) {                       // VTFS lights in the cluster
        color = vtfsHeat(cluster) * (0.4 + 0.6 * albedo);
    } else if (view == 7u) {                // VTFS clusters
        color = vtfsClusterColor(cluster) * (0.5 + 0.5 * max(dot(s.N, V), 0.0));
    }
#ifdef OPAQUE
    else if (view == 1u) {                  // cascades
        int c = int(mask.g * 4.0 + 0.5);
        color = CASCADE_COLORS[clamp(c, 0, 4)] * (0.3 + 0.7 * sunLit);
    } else if (view == 2u) {                // shadow mask
        color = vec3(sunLit);
    } else if (view == 3u) {                // normals
        color = s.N * 0.5 + 0.5;
    } else if (view == 4u) {                // ambient occlusion
        color = vec3(ao);
    } else if (view == 6u) {                // motion vectors: direction as hue, 10 pixels a frame at full brightness
        vec2 motion = textureLod(gVelocity, uv, 0.0).xy * camera.resolution;
        float amount = clamp(length(motion) / 10.0, 0.0, 1.0);
        vec2 d = motion / max(length(motion), 1e-4);
        color = (vec3(0.5) + 0.5 * vec3(d.x, d.y, -d.x)) * amount;
    }
#endif
    outColor = vec4(color, s.baseColor.a);
}
