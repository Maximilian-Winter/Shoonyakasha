#version 450
//
// Tonemap Fragment Shader — HDR to LDR Conversion
//
// 白虎司斷 — The White Tiger governs cutting
// Compresses the infinite range of light into displayable values
//

// HDR input texture (set 0)
layout(set = 0, binding = 0) uniform sampler2D hdrColor;

// Input from fullscreen triangle
layout(location = 0) in vec2 fragTexCoord;

// LDR output
layout(location = 0) out vec4 outColor;

// ═══════════════════════════════════════════════════════════════
// Tonemapping Operators
// ═══════════════════════════════════════════════════════════════

// ACES Filmic Tone Mapping
// Attempt to match the Academy Color Encoding System curve
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Simple Reinhard tone mapping
vec3 Reinhard(vec3 hdr) {
    return hdr / (hdr + vec3(1.0));
}

// Uncharted 2 filmic tone mapping
vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2(vec3 color) {
    float exposureBias = 2.0;
    vec3 curr = Uncharted2Tonemap(exposureBias * color);
    vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(11.2));
    return curr * whiteScale;
}

void main() {
    // Sample HDR color
    vec3 hdr = texture(hdrColor, fragTexCoord).rgb;

    // Exposure adjustment (could be a uniform)
    // IBL environments can be very bright - adjust this to taste
    float exposure = 0.4;  // Reduced from 1.0 for better visibility
    hdr *= exposure;

    // Apply ACES filmic tonemapping (best visual quality)
    vec3 mapped = ACESFilm(hdr);

    // Gamma correction (sRGB)
    // Note: If output format is SRGB, the hardware does this automatically
    // Use linear output format (R8G8B8A8_UNORM) and do manual gamma here
    float gamma = 2.2;
    mapped = pow(mapped, vec3(1.0 / gamma));

    outColor = vec4(mapped, 1.0);
}
