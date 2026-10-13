#version 450
// The image: direct light, plus the indirect diffuse irradiance times the
// diffuse albedo (what Fresnel and metalness leave of it), plus the
// reflections times the specular lobe's directional albedo; exposed and
// tone mapped as in the default pipeline (AgX, Khronos PBR Neutral or ACES
// filmic by settings.toneMapper 2, 1, 0). The views show the indirect
// diffuse irradiance, the irradiance cache, the direct light alone, or the
// reflections alone (times the directional albedo).

#include "sk/pbr.glsl"
#include "sk/tonemap.glsl"
#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDirect;
layout(set = 0, binding = 1) uniform sampler2D gAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gNormal;
layout(set = 0, binding = 3) uniform sampler2D gPosition;
layout(set = 0, binding = 4) uniform sampler2D indirect;
layout(set = 0, binding = 5) uniform sampler2D reflections;
layout(set = 0, binding = 6) uniform sampler2D gGeoNormal;
layout(set = 0, binding = 7) uniform sampler2D specLut;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

vec3 lutAt(float NdotV, float roughness) {
    vec2 texel = clamp(vec2(NdotV, roughness), 0.0, 1.0) * 63.0;
    ivec2 i0 = ivec2(floor(texel));
    ivec2 i1 = min(i0 + 1, ivec2(63));
    vec2 f = texel - vec2(i0);
    vec3 a = mix(texelFetch(specLut, i0, 0).xyz, texelFetch(specLut, ivec2(i1.x, i0.y), 0).xyz, f.x);
    vec3 b = mix(texelFetch(specLut, ivec2(i0.x, i1.y), 0).xyz, texelFetch(specLut, i1, 0).xyz, f.x);
    return mix(a, b, f.y);
}

void main() {
    ivec2 px = ivec2(fragTexCoord * vec2(textureSize(gDirect, 0)));
    vec3 direct = texelFetch(gDirect, px, 0).rgb;
    vec4 position = texelFetch(gPosition, px, 0);

    vec3 hdr = direct;
    if (settings.view == VIEW_CACHE || settings.view == VIEW_DIRECT) {
        hdr = direct;
    } else if (position.w == 0.0) {
        hdr = settings.view == VIEW_INDIRECT || settings.view == VIEW_REFLECTIONS ? vec3(0.0) : direct;
    } else {
        vec3 albedo = texelFetch(gAlbedo, px, 0).rgb;
        vec4 normal = texelFetch(gNormal, px, 0);
        float roughness = texelFetch(gGeoNormal, px, 0).w;
        vec3 V = normalize(camera.position.xyz - position.xyz);
        float NdotV = max(dot(normal.xyz, V), 0.0);
        vec3 F0 = baseReflectivity(albedo, normal.w);
        vec3 F = fresnelSchlick(NdotV, F0);
        vec3 gi = texelFetch(indirect, px, 0).rgb;
        vec3 lut = lutAt(NdotV, roughness);
        vec3 specular = texelFetch(reflections, px, 0).rgb * (F0 * lut.x + lut.y);
        if (settings.view == VIEW_INDIRECT) {
            hdr = gi;
        } else if (settings.view == VIEW_REFLECTIONS) {
            hdr = specular;
        } else {
            hdr = direct + gi * (1.0 - F) * (1.0 - normal.w) * albedo + specular;
        }
    }
    hdr *= settings.exposure;
    vec3 mapped = settings.toneMapper == 2u ? AgX(hdr)
                : settings.toneMapper == 1u ? PBRNeutral(hdr)
                                            : ACESFilm(hdr);
    outColor = vec4(max(mapped, 0.0), 1.0);
}
