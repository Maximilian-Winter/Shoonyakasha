#version 450
// Exposure and ACES filmic tonemapping to the swapchain. The swapchain is
// sRGB, so the hardware encodes on write; the dither breaks up banding in
// dark gradients.

#include "common.glsl"
#include "sk/tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(set = 0, binding = 1) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 hdr = textureLod(hdrColor, fragTexCoord, 0.0).rgb;
    vec3 mapped = settings.debugView == 0u ? ACESFilm(hdr * settings.exposure) : clamp(hdr, 0.0, 1.0);
    float dither = (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
    outColor = vec4(max(mapped + dither, 0.0), 1.0);
}
