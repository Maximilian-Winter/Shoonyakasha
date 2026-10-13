#version 450
// The image: direct light plus the indirect irradiance times the diffuse
// albedo (what Fresnel and metalness leave of it), exposed and tone mapped
// to the swapchain as in the default pipeline (AgX, Khronos PBR Neutral or
// ACES filmic by settings.toneMapper 2, 1, 0). The views show the indirect
// irradiance, the irradiance cache, or the direct light alone.

#include "sk/pbr.glsl"
#include "sk/tonemap.glsl"
#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDirect;
layout(set = 0, binding = 1) uniform sampler2D gAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gNormal;
layout(set = 0, binding = 3) uniform sampler2D gPosition;
layout(set = 0, binding = 4) uniform sampler2D indirect;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    ivec2 px = ivec2(fragTexCoord * vec2(textureSize(gDirect, 0)));
    vec3 direct = texelFetch(gDirect, px, 0).rgb;
    vec3 gi = texelFetch(indirect, px, 0).rgb;
    vec4 position = texelFetch(gPosition, px, 0);

    vec3 hdr;
    if (settings.view == VIEW_INDIRECT) {
        hdr = position.w == 0.0 ? vec3(0.0) : gi;
    } else if (settings.view == VIEW_CACHE || settings.view == VIEW_DIRECT) {
        hdr = direct;
    } else {
        hdr = direct;
        if (position.w != 0.0) {
            vec3 albedo = texelFetch(gAlbedo, px, 0).rgb;
            vec4 normal = texelFetch(gNormal, px, 0);
            vec3 V = normalize(camera.position.xyz - position.xyz);
            vec3 F = fresnelSchlick(max(dot(normal.xyz, V), 0.0), baseReflectivity(albedo, normal.w));
            hdr += gi * (1.0 - F) * (1.0 - normal.w) * albedo;
        }
    }
    hdr *= settings.exposure;
    vec3 mapped = settings.toneMapper == 2u ? AgX(hdr)
                : settings.toneMapper == 1u ? PBRNeutral(hdr)
                                            : ACESFilm(hdr);
    outColor = vec4(max(mapped, 0.0), 1.0);
}
