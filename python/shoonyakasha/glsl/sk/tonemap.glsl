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

#endif
