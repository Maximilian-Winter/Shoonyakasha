#version 450
//
// Tonemap Fragment Shader — HDR to LDR with Chrominance Preservation
//
// The key insight: standard tonemapping maps R, G, B independently,
// which drives all channels toward 1.0 equally — destroying saturation.
// Instead, we tonemap only the LUMINANCE and scale the colour channels
// proportionally, preserving hue and saturation even at high brightness.
//
// This is how 100,000 additively-blended particles retain their colours
// even where they overlap densely.
//

// HDR input texture (set 0, binding 0)
layout(set = 0, binding = 0) uniform sampler2D hdrColor;

// Bloom texture (set 0, binding 1) — soft glow from bright pixels
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;

// Input from fullscreen triangle
layout(location = 0) in vec2 fragTexCoord;

// LDR output
layout(location = 0) out vec4 outColor;

// ═══════════════════════════════════════════════════════════════
// ACES Filmic Tone Mapping (single-channel)
// ═══════════════════════════════════════════════════════════════
float ACESFilm(float x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // Sample HDR scene colour
    vec3 hdr = texture(hdrColor, fragTexCoord).rgb;

    // Sample bloom — the soft scattered light
    vec3 bloom = texture(bloomTexture, fragTexCoord).rgb;

    // Composite bloom into the scene
    float bloomIntensity = 0.5;
    hdr += bloom * bloomIntensity;

    // Exposure adjustment — let the colours breathe
    float exposure = 1.8;
    hdr *= exposure;

    // ── Chrominance-preserving tonemapping ──
    // Compute perceptual luminance (Rec. 709)
    float luminance = dot(hdr, vec3(0.2126, 0.7152, 0.0722));

    // Tonemap only the luminance
    float mappedLum = ACESFilm(luminance);

    // Scale all channels by the luminance ratio
    // This preserves the colour ratios (hue + saturation)
    vec3 mapped = hdr * (mappedLum / max(luminance, 0.001));

    // Saturation boost — vivid colours are the goal
    float sat = 1.3;
    float grey = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    mapped = mix(vec3(grey), mapped, sat);

    mapped = clamp(mapped, 0.0, 1.0);

    // No manual gamma — the swapchain is VK_FORMAT_B8G8R8A8_SRGB,
    // so the hardware applies linear → sRGB conversion automatically.
    outColor = vec4(mapped, 1.0);
}
