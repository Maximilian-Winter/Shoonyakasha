#version 450
//
// Tonemap Fragment Shader — HDR to LDR Conversion
//
// 白虎司斷 — The White Tiger governs cutting
// Compresses the infinite range of light into displayable values
//

// HDR input texture (set 0)
layout(set = 0, binding = 0) uniform sampler2D hdrColor;

// Shading controls — dot-path filled from scene.custom.shrine.*
layout(set = 0, binding = 1) uniform ShadingUBO {
    float exposure;
    float iblIntensity;
    float skyBlur;
    float fogDensity;
    float fogStart;
} shading;

// Input from fullscreen triangle
layout(location = 0) in vec2 fragTexCoord;

// LDR output
layout(location = 0) out vec4 outColor;

#include "sk/tonemap.glsl"

void main() {
    // Sample HDR color
    vec3 hdr = texture(hdrColor, fragTexCoord).rgb;

    hdr *= shading.exposure;

    // Apply ACES filmic tonemapping (best visual quality)
    vec3 mapped = ACESFilm(hdr);

    // No manual gamma. This pass writes to the swapchain, which is created as
    // VK_FORMAT_B8G8R8A8_SRGB (VulkanSwapChain::chooseSwapSurfaceFormat), so the
    // hardware applies the linear -> sRGB encode on write. Doing it here as well
    // encoded the image twice and washed out the midtones.
    outColor = vec4(mapped, 1.0);
}
