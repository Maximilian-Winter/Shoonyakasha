// anisotropy.glsl - anisotropic specular (KHR_materials_anisotropy): the
// G-buffer's packing of the direction and strength, the anisotropic GGX
// distribution and height-correlated visibility the extension specifies,
// and the normal the environment's reflection is looked up with.

#ifndef DEFAULT_ANISOTROPY_GLSL
#define DEFAULT_ANISOTROPY_GLSL

#include "sk/pbr.glsl"

// A tangent and bitangent around the unit normal N, the same for the same N
// (Duff et al. 2017).
void normalFrame(vec3 N, out vec3 t, out vec3 b) {
    float s = N.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + N.z);
    float c = N.x * N.y * a;
    t = vec3(1.0 + s * N.x * N.x * a, s * c, -s * N.x);
    b = vec3(c, s + N.y * N.y * a, -N.y);
}

// gNormal's zw: the direction T (perpendicular to N) as its angle in
// normalFrame(N), and the strength. A strength of 0 is no anisotropy.
vec2 packAnisotropy(vec3 N, vec3 T, float strength) {
    vec3 t, b;
    normalFrame(N, t, b);
    return vec2(atan(dot(T, b), dot(T, t)), strength);
}

vec3 unpackAnisotropy(vec3 N, vec2 packed) {
    vec3 t, b;
    normalFrame(N, t, b);
    float angle = isnan(packed.x) || isinf(packed.x) ? 0.0 : packed.x;
    return t * cos(angle) + b * sin(angle);
}

// Roughness along the direction (at) and across it (ab), from the
// perceptual roughness and the strength.
vec2 anisotropicAlphas(float roughness, float strength) {
    float alpha = roughness * roughness;
    return vec2(max(mix(alpha, 1.0, strength * strength), 1e-3), clamp(alpha, 1e-3, 1.0));
}

float distributionGGXAnisotropic(float NdotH, float TdotH, float BdotH, vec2 alphas) {
    float a2 = alphas.x * alphas.y;
    vec3 f = vec3(alphas.y * TdotH, alphas.x * BdotH, a2 * NdotH);
    float w2 = a2 / dot(f, f);
    return a2 * w2 * w2 / SK_PI;
}

// Smith's height-correlated visibility, including 1 / (4 N.L N.V).
float visibilityGGXAnisotropic(float NdotL, float NdotV, float TdotV, float BdotV, float TdotL, float BdotL,
                               vec2 alphas) {
    float GGXV = NdotL * length(vec3(alphas.x * TdotV, alphas.y * BdotV, NdotV));
    float GGXL = NdotV * length(vec3(alphas.x * TdotL, alphas.y * BdotL, NdotL));
    return clamp(0.5 / max(GGXV + GGXL, 1e-7), 0.0, 1.0);
}

// The normal the environment's reflection is looked up with: N bent towards
// the one that faces V across the direction, more for stronger and smoother
// anisotropy.
vec3 anisotropicBentNormal(vec3 N, vec3 V, vec3 B, float roughness, float strength) {
    vec3 across = cross(cross(B, V), B);
    if (dot(across, across) < 1e-8) return N;
    float bend = 1.0 - strength * (1.0 - roughness);
    bend *= bend;
    return normalize(mix(normalize(across), N, bend * bend));
}

#endif
