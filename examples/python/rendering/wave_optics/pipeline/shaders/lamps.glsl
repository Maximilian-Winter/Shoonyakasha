// lamps.glsl - emission spectra of the lamp.
//
// Each spectrum has the luminance (CIE Y, through spectral.glsl's colour
// matching fit) of the spectrum that is 1 at every wavelength. Line spectra
// are sums of Gaussians, (centre nm, standard deviation nm, weight) with
// the weights summing to one:
//   - low-pressure sodium: the D lines, 589.0 and 589.6 nm, 2 : 1;
//   - mercury: 404.7, 435.8, 546.1, 577.0 and 579.1 nm;
//   - RGB LED: three bands at 455, 530 and 625 nm, weighted so that their
//     sum is white.
// The incandescent lamp is a blackbody at 2700 K.

#ifndef LAMPS_GLSL
#define LAMPS_GLSL

const uint LAMP_RGB = 0u;            // the light's own RGB colour
const uint LAMP_INCANDESCENT = 1u;
const uint LAMP_SODIUM = 2u;
const uint LAMP_MERCURY = 3u;
const uint LAMP_LED = 4u;

const vec3 LAMP_LINES[10] = vec3[10](
    vec3(588.995, 0.3, 0.6667), vec3(589.592, 0.3, 0.3333),
    vec3(404.66, 0.3, 0.2), vec3(435.83, 0.3, 0.3), vec3(546.07, 0.3, 0.3),
    vec3(576.96, 0.3, 0.1), vec3(579.07, 0.3, 0.1),
    vec3(455.0, 9.0, 0.2475), vec3(530.0, 13.0, 0.3656), vec3(625.0, 7.0, 0.3870));

// The lines of a spectrum: the first and one past the last of LAMP_LINES.
uvec2 lampLines(uint type) {
    if (type == LAMP_SODIUM) return uvec2(0u, 2u);
    if (type == LAMP_MERCURY) return uvec2(2u, 7u);
    if (type == LAMP_LED) return uvec2(7u, 10u);
    return uvec2(0u, 0u);
}

float lampScale(uint type) {
    if (type == LAMP_SODIUM) return 138.3915;
    if (type == LAMP_MERCURY) return 223.6423;
    return 245.8910;
}

float gaussianDensity(float x, float mu, float sigma) {
    float t = (x - mu) / sigma;
    return exp(-0.5 * t * t) / (sigma * 2.50662827);
}

// The sum of a line spectrum's Gaussians at lambda, weights summing to one.
float lineDensity(uint type, float lambda) {
    uvec2 range = lampLines(type);
    float sum = 0.0;
    for (uint i = range.x; i < range.y; ++i) {
        vec3 line = LAMP_LINES[i];
        sum += line.z * gaussianDensity(lambda, line.x, line.y);
    }
    return sum;
}

float planck(float lambda, float temperature) {
    float m = lambda * 1e-9;
    return 1.0 / (m * m * m * m * m * (exp(0.0143877735 / (m * temperature)) - 1.0));
}

// The lamp's spectral emission at lambda (types other than LAMP_RGB).
float lampSpectrum(uint type, float lambda) {
    if (type == LAMP_INCANDESCENT) return 0.9801 * planck(lambda, 2700.0) / planck(560.0, 2700.0);
    return lampScale(type) * lineDensity(type, lambda);
}

#endif
