// surface.glsl - a glTF metallic-roughness material evaluated at a fragment.
//
// The including fragment shader declares the material textures at set 1 and
// includes material_draw.glsl, and takes the gbuffer.vert outputs as inputs.
// It defines EXPOSURE_SET, the set where it binds pipeline.json's
// exposureReadSet: the settings and the adapted exposure.

#ifndef DEFAULT_SURFACE_GLSL
#define DEFAULT_SURFACE_GLSL

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;
layout(set = 1, binding = 5) uniform sampler2D clearcoatMap;            // KHR_materials_clearcoat: r amount
layout(set = 1, binding = 6) uniform sampler2D clearcoatRoughnessMap;   // g roughness
layout(set = 1, binding = 10) uniform sampler2D sheenColorMap;          // KHR_materials_sheen: rgb colour
layout(set = 1, binding = 11) uniform sampler2D sheenRoughnessMap;      // a roughness
layout(set = 1, binding = 12) uniform sampler2D specularMap;            // KHR_materials_specular: a strength
layout(set = 1, binding = 13) uniform sampler2D specularColorMap;       // rgb colour
#include "material_data.glsl"

layout(set = EXPOSURE_SET, binding = 0) uniform ExposureSettings { DEFAULT_SETTINGS_BLOCK } exposureSettings;
layout(set = EXPOSURE_SET, binding = 1) uniform sampler2D exposureImage;   // 1x1, last frame's adapted value

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragColor;
layout(location = 6) in vec4 fragWorldTangent;   // 0 for meshes without uvs
layout(location = 7) in vec2 fragTexCoord1;      // TEXCOORD_1

// The uv the material texture `name` (albedo, normal, ...) is sampled at.
#define MATERIAL_UV(name) textureUv(materialData.name##Uv0, materialData.name##Uv1, fragTexCoord, fragTexCoord1)

struct Surface {
    vec4 baseColor;   // rgb albedo, a coverage
    vec3 N;           // world normal, facing the viewer
    float metallic;
    float roughness;
    float occlusion;
    vec3 emissive;
    float clearcoat;            // amount of the clear lacquer layer over the base
    float clearcoatRoughness;
    vec3 sheenColor;            // black for no sheen
    float sheenRoughness;
    vec3 dielectricF0;          // the base's F0 where it is not metal: from ior, specular colour and strength
    float specularWeight;       // the dielectric's F90: specular strength
};

// Tangent frame from screen-space derivatives, for meshes without tangents.
mat3 derivativeTBN(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invMax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return mat3(T * invMax, B * invMax, N);
}

// Tangent frame at the fragment for a normal map sampled at `uv`: the
// mesh's tangent made orthogonal to N, or the derivative frame when the mesh
// has none or the map is not sampled at TEXCOORD_0 as it is.
mat3 tangentFrame(vec3 N, vec2 uv) {
    if (!isPlainUv(materialData.normalUv0, materialData.normalUv1)) return derivativeTBN(N, fragWorldPos, uv);
    vec3 T = fragWorldTangent.xyz - N * dot(N, fragWorldTangent.xyz);
    if (dot(T, T) < 1e-12) return derivativeTBN(N, fragWorldPos, uv);
    T = normalize(T);
    float side = fragWorldTangent.w < 0.0 ? -1.0 : 1.0;
    return mat3(T, side * cross(N, T), N);
}

// The exposure Tonemap applies, as of last frame: settings.exposure times
// the adapted value when automatic exposure is on.
float sceneExposure() {
    float exposure = exposureSettings.exposure;
    if (exposureSettings.autoExposure != 0u) {
        float adapted = texelFetch(exposureImage, ivec2(0), 0).r;
        if (adapted > 0.0 && !isinf(adapted)) exposure *= adapted;
    }
    return max(exposure, 1e-6);
}

vec4 surfaceBaseColor() {
    return texture(albedoMap, MATERIAL_UV(albedo)) * draw.baseColorFactor * vec4(fragColor, 1.0);
}

Surface evaluateSurface(vec4 baseColor) {
    Surface s;
    s.baseColor = baseColor;

    vec3 N = normalize(fragWorldNormal);
    if (draw.hasNormalMap > 0.5) {
        // z from x and y: a block-compressed (BC5) map holds only those two.
        vec2 uv = MATERIAL_UV(normal);
        vec2 xy = texture(normalMap, uv).xy * 2.0 - 1.0;
        vec3 t = vec3(xy * draw.normalScale, sqrt(max(1.0 - dot(xy, xy), 0.0)));
        vec3 mapped = tangentFrame(N, uv) * t;
        if (dot(mapped, mapped) > 1e-6) N = normalize(mapped);
    }
    // Double-sided materials, and single-sided ones seen from behind, shade
    // the side facing the camera.
    if (!gl_FrontFacing) N = -N;
    s.N = N;

    s.metallic = draw.metallicFactor;
    s.roughness = draw.roughnessFactor;
    if (draw.hasMetalRoughMap > 0.5) {
        vec4 mr = texture(metallicRoughnessMap, MATERIAL_UV(metallicRoughness));   // glTF: g roughness, b metallic
        s.metallic *= mr.b;
        s.roughness *= mr.g;
    }
    s.metallic = clamp(s.metallic, 0.0, 1.0);
    s.roughness = clamp(s.roughness, 0.045, 1.0);

    // No texture: the fallback is 1.
    s.occlusion = 1.0 + draw.occlusionStrength * (texture(aoMap, MATERIAL_UV(occlusion)).r - 1.0);
    s.emissive = texture(emissiveMap, MATERIAL_UV(emissive)).rgb * draw.emissiveFactor.rgb;
    // With compensation 1 the emission reaches the tone curve at its own
    // value, whatever the exposure.
    if (draw.exposureCompensation > 0.0) {
        s.emissive *= mix(1.0, 1.0 / sceneExposure(), clamp(draw.exposureCompensation, 0.0, 1.0));
    }
    // The coat maps scale the factors; without them the fallback is white.
    s.clearcoat = clamp(draw.clearcoatFactor, 0.0, 1.0);
    s.clearcoatRoughness = draw.clearcoatRoughness;
    if (s.clearcoat > 0.0) {
        s.clearcoat *= texture(clearcoatMap, MATERIAL_UV(clearcoat)).r;
        s.clearcoatRoughness *= texture(clearcoatRoughnessMap, MATERIAL_UV(clearcoatRoughness)).g;
    }
    s.clearcoatRoughness = clamp(s.clearcoatRoughness, 0.045, 1.0);

    // The textures scale the factors; without them the fallback is white.
    s.sheenColor = materialData.sheenColorFactor.rgb;
    s.sheenRoughness = materialData.sheenRoughnessFactor;
    if (max(s.sheenColor.r, max(s.sheenColor.g, s.sheenColor.b)) > 0.0) {
        s.sheenColor *= texture(sheenColorMap, MATERIAL_UV(sheenColor)).rgb;
        s.sheenRoughness *= texture(sheenRoughnessMap, MATERIAL_UV(sheenRoughness)).a;
    }
    s.sheenColor = clamp(s.sheenColor, 0.0, 1.0);
    s.sheenRoughness = clamp(s.sheenRoughness, 0.0, 1.0);

    float ior = max(draw.ior, 1.0);
    float f0 = (ior - 1.0) / (ior + 1.0);
    s.specularWeight = clamp(materialData.specularFactor * texture(specularMap, MATERIAL_UV(specular)).a, 0.0, 1.0);
    vec3 specularColor = materialData.specularColorFactor.rgb * texture(specularColorMap, MATERIAL_UV(specularColor)).rgb;
    s.dielectricF0 = min(f0 * f0 * specularColor, vec3(1.0)) * s.specularWeight;
    return s;
}

#endif
