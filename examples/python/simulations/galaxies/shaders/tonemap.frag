#version 450
// The HDR image plus `bloom` times the bloom chain's sum of levels, times
// `exposure`, to the swapchain (sRGB, encoded by the hardware on write).
// The ACES filmic curve maps the colour's luminance, and the colour is
// scaled to it, keeping its hue; what a channel then has beyond 1 is spread
// over the others, so the brightest cores turn white gradually.

#include "sk/tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(set = 0, binding = 1) uniform sampler2D bloomChain;   // mip 0: every level summed
layout(set = 0, binding = 2) uniform Post {
    float exposure;
    float bloom;
} post;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float BLOOM_LEVELS = 6.0;   // mips of bloomChain in pipeline/resources.json
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main() {
    vec3 hdr = textureLod(hdrColor, fragTexCoord, 0.0).rgb;
    vec3 glow = textureLod(bloomChain, fragTexCoord, 0.0).rgb / BLOOM_LEVELS;
    vec3 c = (hdr + post.bloom * glow) * post.exposure;
    float l = dot(c, LUMA);
    vec3 mapped = l > 0.0 ? c * (ACESFilm(vec3(l)).r / l) : vec3(0.0);
    float excess = max(max(mapped.r, max(mapped.g, mapped.b)) - 1.0, 0.0);
    outColor = vec4(min(mapped + excess, vec3(1.0)), 1.0);
}
