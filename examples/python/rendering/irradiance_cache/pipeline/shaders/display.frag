#version 450
// The averaged image, exposed and tone mapped to the swapchain: AgX,
// Khronos PBR Neutral or ACES filmic, by settings.toneMapper (2, 1, 0), as
// in the default pipeline. The entries view is shown as it is.

#include "sk/tonemap.glsl"
#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D image;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 hdr = textureLod(image, fragTexCoord, 0.0).rgb;
    if (settings.view == VIEW_ENTRIES) {
        outColor = vec4(hdr, 1.0);
        return;
    }
    hdr *= settings.exposure;
    vec3 mapped = settings.toneMapper == 2u ? AgX(hdr)
                : settings.toneMapper == 1u ? PBRNeutral(hdr)
                                            : ACESFilm(hdr);
    outColor = vec4(max(mapped, 0.0), 1.0);
}
