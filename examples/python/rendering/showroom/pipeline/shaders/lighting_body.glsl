// lighting_body.glsl - deferred lighting: every scene light plus image-based
// light for each G-buffer pixel, the sky where there is none. Added onto the
// emission the G-buffer pass left in the HDR target. Built as lighting.frag
// (light shadows from the atlas) and lighting_rt.frag (RT_LOCAL_SHADOWS:
// shadow rays towards each shadowed spot and point light).

#include "common.glsl"
#include "sk/pbr.glsl"

layout(set = 0, binding = 0) uniform sampler2D gAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gMaterial;
layout(set = 0, binding = 3) uniform sampler2D gDepth;
layout(set = 0, binding = 4) uniform sampler2D shadowMask;
layout(set = 0, binding = 5) uniform sampler2D aoMap;
layout(set = 0, binding = 6) uniform sampler2D gVelocity;
layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube prefilterMap;
layout(set = 1, binding = 2) uniform sampler2D brdfLUT;
layout(set = 2, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 3, binding = 0) uniform Lights { DEFAULT_LIGHTS_BLOCK };
layout(set = 3, binding = 1) uniform LocalShadowMatrices { DEFAULT_LOCAL_SHADOWS_BLOCK } localShadows;
layout(set = 3, binding = 2) uniform sampler2DShadow localShadowAtlas;
layout(set = 3, binding = 3) uniform usampler2D lightClusters;
layout(std430, set = 3, binding = 4) readonly buffer LtcTable { vec4 ltcTable[]; };
layout(set = 4, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 4, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;
layout(set = 4, binding = 2) uniform Showroom { SHOWROOM_BLOCK } showroom;
#ifdef RT_LOCAL_SHADOWS
layout(set = 5, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 6, binding = 0) uniform sampler2D taaHistory;
#define RT_REFLECTIONS
#endif

#include "vsm.glsl"

#define LOCAL_SHADOWS
#define CLUSTERED
#include "lights.glsl"

#ifdef RT_REFLECTIONS
// Ray-traced reflections of what is on screen. A ray along the reflection
// (spread by roughness, so TAA averages a glossy lobe) finds the nearest
// surface in the acceleration structure. Where that point is on screen and
// not hidden there, last frame's image at it is the reflected light;
// elsewhere the reflection is blocked by something the screen cannot show
// (a car's underside in a glossy floor) and the environment behind it is
// darkened instead. Rays that hit nothing keep the environment.
//
// Returns the weight to give the traced light over the environment's, and
// the light in `radiance`.
float tracedReflection(vec3 worldPos, vec3 N, vec3 V, float roughness, float viewDepth, vec3 environment,
                       out vec3 radiance) {
    radiance = vec3(0.0);
    float weight = (1.0 - smoothstep(0.3, 0.55, roughness)) * showroom.reflections;
    if (weight <= 0.0) return 0.0;

    vec3 R = reflect(-V, N);
    float u = temporalNoise(gl_FragCoord.xy + vec2(91.0, 13.0), camera.frame + 11u, camera.taa);
    float v = temporalNoise(gl_FragCoord.yx + vec2(7.0, 53.0), camera.frame + 29u, camera.taa);
    float spread = roughness * roughness;
    float z = 1.0 - 2.0 * u, r = sqrt(max(1.0 - z * z, 0.0)), a = 6.28318531 * v;
    vec3 dir = normalize(R + spread * vec3(r * cos(a), r * sin(a), z));
    if (dot(dir, N) <= 0.0) dir = R;

    vec3 origin = worldPos + N * (0.01 + 0.002 * viewDepth);
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.0, dir, 200.0);
    while (rayQueryProceedEXT(query)) {}
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) return 0.0;
    vec3 hit = origin + dir * rayQueryGetIntersectionTEXT(query, true);

    // Blocked unless shown otherwise.
    radiance = environment * 0.15;
    vec4 clip = camera.proj * camera.view * vec4(hit, 1.0);
    if (clip.w > 0.0) {
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        vec2 edge = min(uv, 1.0 - uv);
        if (all(greaterThan(edge, vec2(0.0)))) {
            float sceneDepth = -viewPositionFromDepth(camera.invProj, uv, textureLod(gDepth, uv, 0.0).r).z;
            float hitDepth = -(camera.view * vec4(hit, 1.0)).z;
            if (abs(sceneDepth - hitDepth) < 0.03 + 0.02 * hitDepth) {
                vec4 previous = camera.prevViewProj * vec4(hit, 1.0);
                vec2 prevUV = previous.xy / previous.w * 0.5 + 0.5;
                vec4 seen = textureLod(taaHistory, prevUV, 0.0);
                float fade = smoothstep(0.0, 0.08, min(edge.x, edge.y));
                if (seen.a > 0.0) radiance = mix(radiance, seen.rgb, fade);
            }
        }
    }
    return weight;
}
#endif

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const vec3 CASCADE_COLORS[5] = vec3[](vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3), vec3(0.3, 0.5, 1.0),
                                      vec3(1.0, 1.0, 0.3), vec3(1.0));
const vec3 LEVEL_COLORS[8] = vec3[](vec3(1.0, 0.3, 0.3), vec3(1.0, 0.65, 0.2), vec3(1.0, 1.0, 0.3),
                                    vec3(0.3, 1.0, 0.3), vec3(0.3, 1.0, 1.0), vec3(0.3, 0.5, 1.0),
                                    vec3(0.75, 0.4, 1.0), vec3(1.0, 0.45, 0.8));

// 1 on the borders of the virtual shadow map's pages (and, fainter, its
// texels when they are larger than a few pixels), for the debug views.
float pageGrid(vec3 worldPos, int level) {
    VsmBasis b = vsmBasis();
    vec2 texel = vsmLightSpace(b, worldPos).xy / vsmTexelSize(level);
    vec2 page = texel / float(VSM_PAGE_SIZE);
    vec2 pageWidth = fwidth(page);
    vec2 pageLine = smoothstep(pageWidth * 1.5, vec2(0.0), min(fract(page), 1.0 - fract(page)));
    vec2 texelWidth = fwidth(texel);
    vec2 texelLine = smoothstep(texelWidth * 1.5, vec2(0.0), min(fract(texel), 1.0 - fract(texel)))
                   * step(texelWidth, vec2(0.25));
    return max(max(pageLine.x, pageLine.y), 0.35 * max(texelLine.x, texelLine.y));
}

void main() {
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    vec3 viewPos = viewPositionFromDepth(camera.invProj, fragTexCoord, depth);

    if (depth >= 1.0) {
        vec3 dir = normalize(mat3(camera.invView) * viewPos);
        outColor = vec4(textureLod(prefilterMap, dir, settings.skyBlur).rgb * settings.iblIntensity, 1.0);
        return;
    }

    vec4 albedoAO = textureLod(gAlbedo, fragTexCoord, 0.0);
    vec3 albedo = albedoAO.rgb;
    float occlusion = albedoAO.a;
    vec3 N = octDecode(textureLod(gNormal, fragTexCoord, 0.0).xy);
    vec4 material = textureLod(gMaterial, fragTexCoord, 0.0);
    float metallic = material.r;
    float roughness = max(material.g, 0.045);
    coatFactor = material.b;
    coatRoughness = max(material.a, 0.045);
    vec2 mask = textureLod(shadowMask, fragTexCoord, 0.0).rg;
    float sunVisibility = mask.r;

    vec3 worldPos = (camera.invView * vec4(viewPos, 1.0)).xyz;
    vec3 V = normalize(camera.position.xyz - worldPos);
    vec3 F0 = baseReflectivity(albedo, metallic);

    int sunIndex = cascades.sunEnabled != 0u ? cascades.sunLightIndex : -1;
#ifdef RT_LOCAL_SHADOWS
    rtLightPixel = gl_FragCoord.xy;
    rtLightOffset = 0.01 + 0.002 * -viewPos.z;
#endif
    uint cluster = clusterIndex(fragTexCoord, -viewPos.z, camera.params.x, camera.params.y);
    vec3 direct = directLight(cluster, worldPos, N, V, albedo, metallic, roughness, F0, sunIndex, sunVisibility);
    // Screen-space and material occlusion, with the light that bounces
    // between occluders added back for bright albedo (Jimenez 2016), and
    // occlusion of reflections from it (Lagarde 2014).
    float ao = min(textureLod(aoMap, fragTexCoord, 0.0).r, occlusion);
    vec3 a = 2.0404 * albedo - 0.3324, b = -4.7951 * albedo + 0.6417, c = 2.7552 * albedo + 0.6903;
    vec3 diffuseAO = max(vec3(ao), ((ao * a + b) * ao + c) * ao);
    float NdotV = max(dot(N, V), 1e-4);
    float specularAO = clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);

    vec3 ambientDiffuse, ambientSpecular;
    ambientLightParts(irradianceMap, prefilterMap, brdfLUT, N, V, albedo, metallic, roughness, F0,
                      ambientDiffuse, ambientSpecular);
    vec3 ambient = (ambientDiffuse * diffuseAO + ambientSpecular * specularAO)
                 * settings.iblIntensity * mix(settings.shadowAmbient, 1.0, sunVisibility);
#ifdef RT_REFLECTIONS
    {
        // A clear coat is the glossiest layer, and the one whose reflection
        // shows: trace it in place of the base's when there is one. Car
        // paint's base is often too rough to trace at all.
        bool coated = coatFactor > 0.0;
        float traceRoughness = coated ? coatRoughness : roughness;
        vec2 envBRDF = texture(brdfLUT, vec2(NdotV, traceRoughness)).rg;
        vec3 split = coated ? vec3((COAT_F0 * envBRDF.x + envBRDF.y) * coatFactor) : F0 * envBRDF.x + envBRDF.y;
        float maxLod = float(textureQueryLevels(prefilterMap) - 1);
        vec3 environment = textureLod(prefilterMap, reflect(-V, N), traceRoughness * maxLod).rgb * settings.iblIntensity;
        vec3 traced;
        float weight = tracedReflection(worldPos, N, V, traceRoughness, -viewPos.z, environment, traced);
        if (weight > 0.0) {
            vec3 replaced = coated ? ambientCoatSpecular : ambientSpecular;
            vec3 envSpecular = replaced * specularAO * settings.iblIntensity
                             * mix(settings.shadowAmbient, 1.0, sunVisibility);
            ambient += (traced * split * max(specularAO, 0.5) - envSpecular) * weight;
        }
    }
#endif

    vec3 color = direct + ambient;

    // The shadow mask's green: the cascade / 4 with the cascades, the
    // virtual shadow map's vsmDebugCode with it.
    int code = int(mask.g * 16.0 + 0.5);
    int vsmLevel = code >= 16 ? -1 : code & 7;
    bool vsmDrawn = code < 16 && code >= 8;
    bool vsmOn = showroom.sunShadowMode != 0u;
    if (settings.debugView == 1u && !vsmOn) {          // cascades
        int c = int(mask.g * 4.0 + 0.5);
        color = CASCADE_COLORS[clamp(c, 0, 4)] * (0.3 + 0.7 * sunVisibility);
    } else if (settings.debugView == 1u) {             // virtual shadow map levels, page borders
        vec3 tint = vsmLevel < 0 ? vec3(1.0) : LEVEL_COLORS[vsmLevel];
        float grid = vsmLevel < 0 ? 0.0 : pageGrid(worldPos, vsmLevel);
        color = mix(tint * (0.3 + 0.7 * sunVisibility), vec3(0.05), grid * 0.8);
    } else if (settings.debugView == 7u) {             // virtual shadow map pages: drawn this frame or kept
        vec3 tint = vsmLevel < 0 ? vec3(0.5) : (vsmDrawn ? vec3(1.0, 0.35, 0.25) : vec3(0.35, 0.9, 0.45));
        float grid = vsmLevel < 0 ? 0.0 : pageGrid(worldPos, vsmLevel);
        color = mix(tint * (0.25 + 0.75 * sunVisibility) * (0.4 + 0.6 * albedo), vec3(0.05), grid * 0.8);
    } else if (settings.debugView == 2u) {   // shadow mask
        color = vec3(sunVisibility);
    } else if (settings.debugView == 3u) {   // normals
        color = N * 0.5 + 0.5;
    } else if (settings.debugView == 4u) {   // ambient occlusion
        color = vec3(ao);
    } else if (settings.debugView == 5u) {   // lights in the pixel's cluster: blue 0, green 8, red 16+
        float n = float(texelFetch(lightClusters, ivec2(int(cluster), 0), 0).r) / 16.0;
        color = clamp(vec3(n * 2.0 - 1.0, 1.0 - abs(n * 2.0 - 1.0), 1.0 - n * 2.0), 0.0, 1.0) * (0.4 + 0.6 * albedo);
    } else if (settings.debugView == 6u) {   // motion vectors: direction as hue, 10 pixels a frame at full brightness
        vec2 motion = textureLod(gVelocity, fragTexCoord, 0.0).xy * camera.resolution;
        float amount = clamp(length(motion) / 10.0, 0.0, 1.0);
        vec2 d = motion / max(length(motion), 1e-4);
        color = (vec3(0.5) + 0.5 * vec3(d.x, d.y, -d.x)) * amount;
    }
    outColor = vec4(color, 1.0);
}
