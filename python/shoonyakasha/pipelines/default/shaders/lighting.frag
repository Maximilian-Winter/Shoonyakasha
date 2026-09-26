#version 450
// Deferred lighting: every scene light plus image-based light for each
// G-buffer pixel, the sky where there is none. Added onto the emission the
// G-buffer pass left in the HDR target.

#include "common.glsl"
#include "sk/pbr.glsl"

layout(set = 0, binding = 0) uniform sampler2D gAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gMaterial;
layout(set = 0, binding = 3) uniform sampler2D gDepth;
layout(set = 0, binding = 4) uniform sampler2D shadowMask;
layout(set = 0, binding = 5) uniform sampler2D aoMap;
layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube prefilterMap;
layout(set = 1, binding = 2) uniform sampler2D brdfLUT;
layout(set = 2, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 3, binding = 0) uniform Lights { DEFAULT_LIGHTS_BLOCK };
layout(set = 4, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 4, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;

#include "lights.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const vec3 CASCADE_COLORS[5] = vec3[](vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3), vec3(0.3, 0.5, 1.0),
                                      vec3(1.0, 1.0, 0.3), vec3(1.0));

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
    vec2 material = textureLod(gMaterial, fragTexCoord, 0.0).rg;
    float metallic = material.r;
    float roughness = max(material.g, 0.045);
    vec2 mask = textureLod(shadowMask, fragTexCoord, 0.0).rg;
    float sunVisibility = mask.r;

    vec3 worldPos = (camera.invView * vec4(viewPos, 1.0)).xyz;
    vec3 V = normalize(camera.position.xyz - worldPos);
    vec3 F0 = baseReflectivity(albedo, metallic);

    int sunIndex = cascades.sunEnabled != 0u ? cascades.sunLightIndex : -1;
    vec3 direct = directLight(worldPos, N, V, albedo, metallic, roughness, F0, sunIndex, sunVisibility);
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

    vec3 color = direct + ambient;

    if (settings.debugView == 1u) {          // cascades
        int c = int(mask.g * 4.0 + 0.5);
        color = CASCADE_COLORS[clamp(c, 0, 4)] * (0.3 + 0.7 * sunVisibility);
    } else if (settings.debugView == 2u) {   // shadow mask
        color = vec3(sunVisibility);
    } else if (settings.debugView == 3u) {   // normals
        color = N * 0.5 + 0.5;
    } else if (settings.debugView == 4u) {   // ambient occlusion
        color = vec3(ao);
    }
    outColor = vec4(color, 1.0);
}
