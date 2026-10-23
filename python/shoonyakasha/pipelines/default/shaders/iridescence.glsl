// iridescence.glsl - the reflectance of a thin film over a surface
// (KHR_materials_iridescence), after Belcour and Barla 2017, "A Practical
// Extension to Microfacet Theory for the Modeling of Varying Iridescence":
// the two interfaces' Fresnel terms, summed as an Airy series over the
// film's optical path difference, integrated against the CIE colour
// matching functions through their Gaussian fit, in linear sRGB.

#ifndef DEFAULT_IRIDESCENCE_GLSL
#define DEFAULT_IRIDESCENCE_GLSL

#include "sk/pbr.glsl"

// CIE XYZ to linear sRGB (Rec. 709 primaries, D65).
const mat3 FILM_XYZ_TO_SRGB = mat3(3.2404542, -0.9692660, 0.0556434,
                                   -1.5371385, 1.8760108, -0.2040259,
                                   -0.4985314, 0.0415560, 1.0572252);

float filmSquare(float x) { return x * x; }
vec3 filmSquare(vec3 x) { return x * x; }

float filmSchlick(float f0, float cosTheta) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 filmSchlick(vec3 f0, float cosTheta) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// The colour of interference at optical path difference `opd` (nm) with
// phase shift `shift` per channel: the Fourier transform of the colour
// matching functions' Gaussian fit, in linear sRGB.
vec3 filmSensitivity(float opd, vec3 shift) {
    float phase = 2.0 * SK_PI * opd * 1.0e-9;
    const vec3 amplitude = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    const vec3 position = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
    const vec3 variance = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);
    vec3 xyz = amplitude * sqrt(2.0 * SK_PI * variance) * cos(position * phase + shift)
             * exp(-phase * phase * variance);
    xyz.x += 9.7470e-14 * sqrt(2.0 * SK_PI * 4.5282e+09) * cos(2.2399e+06 * phase + shift.x)
           * exp(-4.5282e+09 * phase * phase);
    return FILM_XYZ_TO_SRGB * (xyz / 1.0685e-7);
}

// Reflectance of a film of index filmIor and thickness `thickness` (nm), seen
// at cosine cosTheta from a medium of index outsideIor, over a base whose
// reflectance at normal incidence is baseF0.
vec3 thinFilmFresnel(float outsideIor, float filmIor, float cosTheta, float thickness, vec3 baseF0) {
    // A film thinner than about 0.03 nm fades to the outside medium.
    float eta2 = mix(outsideIor, filmIor, smoothstep(0.0, 0.03, thickness));
    float sinTheta2Sq = filmSquare(outsideIor / eta2) * (1.0 - cosTheta * cosTheta);
    float cosTheta2Sq = 1.0 - sinTheta2Sq;
    if (cosTheta2Sq < 0.0) return vec3(1.0);   // total internal reflection
    float cosTheta2 = sqrt(cosTheta2Sq);

    // Top interface, outside to film.
    float R12 = filmSchlick(filmSquare((eta2 - outsideIor) / (eta2 + outsideIor)), cosTheta);
    float T121 = 1.0 - R12;
    float phi12 = eta2 < outsideIor ? SK_PI : 0.0;
    float phi21 = SK_PI - phi12;

    // Bottom interface, film to base, the base's index from its F0.
    vec3 sqrtF0 = sqrt(clamp(baseF0, 0.0, 0.9999));
    vec3 baseIor = (1.0 + sqrtF0) / (1.0 - sqrtF0);
    vec3 R23 = filmSchlick(filmSquare((baseIor - eta2) / (baseIor + eta2)), cosTheta2);
    vec3 phi23 = vec3(baseIor.r < eta2 ? SK_PI : 0.0, baseIor.g < eta2 ? SK_PI : 0.0,
                      baseIor.b < eta2 ? SK_PI : 0.0);

    // The Airy sum: the direct reflection, then two orders of interference.
    float opd = 2.0 * eta2 * thickness * cosTheta2;
    vec3 phi = vec3(phi21) + phi23;
    vec3 R123 = clamp(R12 * R23, 1e-5, 0.9999);
    vec3 r123 = sqrt(R123);
    vec3 Rs = T121 * T121 * R23 / (1.0 - R123);
    vec3 result = R12 + Rs;
    vec3 Cm = Rs - T121;
    for (int m = 1; m <= 2; ++m) {
        Cm *= r123;
        result += Cm * 2.0 * filmSensitivity(float(m) * opd, float(m) * phi);
    }
    return max(result, vec3(0.0));
}

// The G-buffer's gIridescence (RGBA8 unorm): r amount, g and b the
// thickness over 0 to 2000 nm in 16 bits (g the high byte), a the film's
// index of refraction over 1 to 2.5.
const float FILM_MAX_THICKNESS = 2000.0;

vec4 packFilm(float amount, float filmIor, float thickness) {
    float t = round(clamp(thickness / FILM_MAX_THICKNESS, 0.0, 1.0) * 65535.0);
    float high = floor(t / 256.0);
    return vec4(amount, high / 255.0, (t - high * 256.0) / 255.0, clamp((filmIor - 1.0) / 1.5, 0.0, 1.0));
}

void unpackFilm(vec4 packed, out float amount, out float filmIor, out float thickness) {
    amount = packed.r;
    thickness = (round(packed.g * 255.0) * 256.0 + round(packed.b * 255.0)) / 65535.0 * FILM_MAX_THICKNESS;
    filmIor = 1.0 + packed.a * 1.5;
}

#endif
