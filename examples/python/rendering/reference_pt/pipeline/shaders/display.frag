#version 450
// The accumulated image, exposed and tone mapped to the swapchain: AgX,
// Khronos PBR Neutral or ACES filmic, by settings.toneMapper (2, 1, 0), as
// in the default pipeline. The white furnace view (mode 2) shows the raw
// value instead, so a converged furnace is a flat 1.

#include "sk/tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D accumulation;
layout(set = 0, binding = 1) uniform PathTracer {
    uint reset;
    uint maxBounces;
    uint mode;
    uint toneMapper;
    float exposure;
    float environmentIntensity;
    float sunAngle;
    float maxSamples;
} settings;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 hdr = textureLod(accumulation, fragTexCoord, 0.0).rgb;
    if (settings.mode == 2u) {
        // The swapchain is sRGB: 0.5 written is 0.5 linear, shown as 188 of 255.
        outColor = vec4(clamp(hdr * 0.5, 0.0, 1.0), 1.0);
        return;
    }
    hdr *= settings.exposure;
    vec3 mapped = settings.toneMapper == 2u ? AgX(hdr)
                : settings.toneMapper == 1u ? PBRNeutral(hdr)
                                            : ACESFilm(hdr);
    outColor = vec4(max(mapped, 0.0), 1.0);
}
