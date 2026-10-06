#version 450
// Bloom, exposure and ACES filmic tonemapping to the swapchain. The
// swapchain is sRGB, so the hardware encodes on write; the dither breaks up
// banding in dark gradients.

#include "common.glsl"
#include "sk/tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(set = 0, binding = 1) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 0, binding = 2) uniform sampler2D bloom;          // mip 0 of the chain: every level summed
layout(set = 0, binding = 3) uniform sampler2D exposureImage;  // 1x1, from exposure.comp

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float BLOOM_LEVELS = 6.0;   // mips of bloomChain in pipeline.json

void main() {
    vec3 hdr = textureLod(hdrColor, fragTexCoord, 0.0).rgb;
    if (settings.debugView != 0u) {
        outColor = vec4(clamp(hdr, 0.0, 1.0), 1.0);
        return;
    }

    vec3 bloomColor = textureLod(bloom, fragTexCoord, 0.0).rgb / BLOOM_LEVELS;
    hdr = mix(hdr, bloomColor, clamp(settings.bloomIntensity, 0.0, 1.0));

    float exposure = settings.exposure;
    if (settings.autoExposure != 0u) {
        float adapted = texelFetch(exposureImage, ivec2(0), 0).r;
        if (adapted > 0.0 && !isinf(adapted)) exposure *= adapted;
    }

    vec3 mapped = ACESFilm(hdr * exposure);
    float dither = (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
    outColor = vec4(max(mapped + dither, 0.0), 1.0);
}
