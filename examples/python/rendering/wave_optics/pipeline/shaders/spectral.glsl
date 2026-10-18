// spectral.glsl - colour matching and spectra over 380-780 nm.
//
// Colour matching uses the multi-lobe Gaussian fit of the CIE 1931 2-degree
// functions by Wyman, Sloan and Shirley 2013. Linear sRGB is normalised so that the
// equal-energy spectrum is white: a spectrum of 1 everywhere is (1, 1, 1).
// RGB colours become spectra through three smooth bands that sum to one at
// every wavelength (red above 560-620 nm, blue below 470-530 nm, green
// between), so white stays 1 and greys stay flat.

#ifndef SPECTRAL_GLSL
#define SPECTRAL_GLSL

const float LAMBDA_MIN = 380.0;
const float LAMBDA_RANGE = 400.0;

float cmfLobe(float x, float mu, float s1, float s2) {
    float t = (x - mu) / (x < mu ? s1 : s2);
    return exp(-0.5 * t * t);
}

vec3 cieXyz(float lambda) {
    float x = 1.056 * cmfLobe(lambda, 599.8, 37.9, 31.0) + 0.362 * cmfLobe(lambda, 442.0, 16.0, 26.7) -
              0.065 * cmfLobe(lambda, 501.1, 20.4, 26.2);
    float y = 0.821 * cmfLobe(lambda, 568.8, 46.9, 40.5) + 0.286 * cmfLobe(lambda, 530.9, 16.3, 31.1);
    float z = 1.217 * cmfLobe(lambda, 437.0, 11.8, 36.0) + 0.681 * cmfLobe(lambda, 459.0, 26.0, 13.8);
    return vec3(x, y, z);
}

// Linear sRGB of radiance samples at four wavelengths (each drawn with
// density 1 / 400 nm), normalised to the equal-energy white.
vec3 spectrumToRgb(vec4 radiance, vec4 lambdas) {
    vec3 xyz = vec3(0.0);
    for (int i = 0; i < 4; ++i) xyz += radiance[i] * cieXyz(lambdas[i]);
    xyz *= LAMBDA_RANGE / 4.0;
    mat3 toRgb = mat3(3.2406, -0.9689, 0.0557, -1.5372, 1.8758, -0.2040, -0.4986, 0.0415, 1.0570);
    return toRgb * xyz / vec3(128.3627, 101.5486, 97.0496);
}

// The colour of light of one wavelength in linear sRGB at full saturation,
// its largest component 1: the hue of the colour matching functions there,
// that of 420 or 660 nm beyond them.
vec3 wavelengthHue(float lambda) {
    mat3 toRgb = mat3(3.2406, -0.9689, 0.0557, -1.5372, 1.8758, -0.2040, -0.4986, 0.0415, 1.0570);
    vec3 rgb = max(toRgb * cieXyz(clamp(lambda, 420.0, 660.0)), vec3(0.0));
    return rgb / max(max(rgb.r, rgb.g), max(rgb.b, 1e-6));
}

// The value at a wavelength of the spectrum an RGB colour stands for.
float rgbToSpectrum(vec3 rgb, float lambda) {
    float blue = 1.0 - smoothstep(470.0, 530.0, lambda);
    float red = smoothstep(560.0, 620.0, lambda);
    return rgb.r * red + rgb.g * (1.0 - blue - red) + rgb.b * blue;
}

vec4 rgbToSpectrum4(vec3 rgb, vec4 lambdas) {
    return vec4(rgbToSpectrum(rgb, lambdas.x), rgbToSpectrum(rgb, lambdas.y),
                rgbToSpectrum(rgb, lambdas.z), rgbToSpectrum(rgb, lambdas.w));
}

#endif
