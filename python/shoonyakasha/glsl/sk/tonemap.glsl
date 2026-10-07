//
// sk/tonemap.glsl - HDR to display-range tone curves
//
//     #include "sk/tonemap.glsl"
//
// All take linear HDR colour and return linear colour in [0, 1]. None applies
// gamma: the engine's swapchain is sRGB, so the hardware encodes on write.
//

#ifndef SK_TONEMAP_GLSL
#define SK_TONEMAP_GLSL

// Fitted ACES filmic curve.
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 Reinhard(vec3 hdr) {
    return hdr / (hdr + vec3(1.0));
}

vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// Uncharted 2 filmic curve with an exposure bias of 2 and white point 11.2.
vec3 Uncharted2(vec3 color) {
    float exposureBias = 2.0;
    vec3 curr = Uncharted2Tonemap(exposureBias * color);
    vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(11.2));
    return curr * whiteScale;
}

// Khronos PBR Neutral (2024): base colours reproduce as authored up to a
// highlight threshold, above which they compress towards white with slight
// desaturation. Made for product renders, where a paint must stay its colour.
vec3 PBRNeutral(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

// AgX (Troy Sobotka), as fitted by Benjamin Wrensch: a log encoding in an
// inset gamut, then a sigmoid. Bright saturated light rolls off towards
// white instead of skewing in hue, at some cost in saturation.
vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 AgX(vec3 color) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEV = -12.47393, maxEV = 4.026069;
    vec3 v = clamp(log2(max(inset * color, vec3(1e-10))), minEV, maxEV);
    v = agxContrast((v - minEV) / (maxEV - minEV));
    // The curve's output is display-encoded; back to linear for the sRGB swapchain.
    return clamp(pow(max(outset * v, vec3(0.0)), vec3(2.2)), 0.0, 1.0);
}

#endif
