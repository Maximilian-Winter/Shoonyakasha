// specular.glsl - the specular lobe of the materials' BRDF (sk/pbr.glsl's
// cookTorrance: GGX distribution of alpha = roughness^2, Schlick-GGX
// geometry with k = (roughness + 1)^2 / 8, Schlick Fresnel), sampled
// through GGX's visible normals (Heitz 2018), and its directional albedo.
//
// Directions are in the tangent frame of the normal (z up): wo towards the
// viewer, wi towards the light. Densities are per projected solid angle
// (per solid angle divided by wi.z), as in kajiya; `valueOverPdf` is the
// BRDF times wi.z over the density per solid angle.

#ifndef SPECULAR_GLSL
#define SPECULAR_GLSL

const float RTR_ALPHA_CLAMP = 6e-4;   // the lowest alpha reflections use

// The GGX alpha the reflections use for a perceptual roughness.
float specularAlpha(float roughness) { return max(roughness * roughness, RTR_ALPHA_CLAMP); }

float ggxNdf(float a2, float cosTheta) {
    float d = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

// The GGX distribution scaled to peak at 1.
float ggxNdf01(float a2, float cosTheta) {
    float d = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
    return a2 * a2 / (d * d);
}

float smithG1Ggx(float cosTheta, float a2) {
    return 2.0 * cosTheta / (cosTheta + sqrt(a2 + (1.0 - a2) * cosTheta * cosTheta));
}

// sk/pbr.glsl's geometrySmith, for a perceptual roughness from alpha.
float schlickGeometry(float NdotV, float NdotL, float alpha) {
    float r = sqrt(alpha);
    float k = (r + 1.0) * (r + 1.0) / 8.0;
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

struct SpecularValue {
    float pdf;            // per projected solid angle
    vec3 valueOverPdf;
    bool valid;
};

// The density of the visible-normal sampling for wo, per projected solid angle of wi.
float specularPdf(float alpha, vec3 wo, vec3 wi) {
    vec3 h = normalize(wo + wi);
    float a2 = alpha * alpha;
    float pdfH = smithG1Ggx(wo.z, a2) * ggxNdf(a2, h.z) * max(0.0, dot(wo, h)) / wo.z;
    return pdfH / (4.0 * max(dot(wi, h), 1e-8)) / wi.z;
}

SpecularValue specularEvaluate(float alpha, vec3 F0, vec3 wo, vec3 wi) {
    SpecularValue v;
    v.valid = wi.z > 0.0 && wo.z > 0.0;
    if (!v.valid) {
        v.pdf = 0.0;
        v.valueOverPdf = vec3(0.0);
        return v;
    }
    vec3 h = normalize(wo + wi);
    float a2 = alpha * alpha;
    vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - dot(wo, h), 0.0, 1.0), 5.0);
    float G = schlickGeometry(wo.z, wi.z, alpha);
    vec3 value = ggxNdf(a2, h.z) * G * F / (4.0 * wo.z * wi.z);
    v.pdf = specularPdf(alpha, wo, wi);
    v.valueOverPdf = value / max(v.pdf, 1e-20);
    return v;
}

struct SpecularSample {
    vec3 wi;
    float pdf;            // per projected solid angle
    vec3 valueOverPdf;
    bool valid;
};

// A direction drawn from the visible normals of wo.
SpecularSample specularSample(float alpha, vec3 F0, vec3 wo, vec2 u) {
    vec3 vh = normalize(vec3(alpha * wo.x, alpha * wo.y, wo.z));
    vec3 t1 = vh.z < 0.9999 ? normalize(cross(vec3(0.0, 0.0, 1.0), vh)) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = cross(vh, t1);
    float r = sqrt(u.x), phi = 6.28318530718 * u.y;
    float p1 = r * cos(phi), p2 = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
    vec3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
    vec3 m = normalize(vec3(alpha * nh.x, alpha * nh.y, max(0.0, nh.z)));

    SpecularSample result;
    result.wi = reflect(-wo, m);
    result.valid = m.z > 1e-5 && result.wi.z > 1e-5 && wo.z > 1e-5;
    if (!result.valid) {
        result.pdf = 0.0;
        result.valueOverPdf = vec3(0.0);
        return result;
    }
    SpecularValue v = specularEvaluate(alpha, F0, wo, result.wi);
    result.pdf = v.pdf;
    result.valueOverPdf = v.valueOverPdf;
    return result;
}

// The lobe that a viewer at v mostly sees reflected (kajiya's).
vec3 specularDominantDirection(vec3 n, vec3 v, float alpha) {
    vec3 r = reflect(-v, n);
    float f = (1.0 - alpha) * (sqrt(1.0 - alpha) + alpha);
    return normalize(mix(n, r, f));
}

// Reading the directional albedo table `specLut` (64 x 64, over NdotV and
// perceptual roughness): x and y such that the lobe reflects F0 * x + y of
// light from all directions, and z the fraction of drawn directions that
// are valid. The including shader declares `specLut` and defines SPEC_LUT_IMAGE.
#ifdef SPEC_LUT_IMAGE
vec3 specularLutAt(float NdotV, float roughness) {
    vec2 texel = clamp(vec2(NdotV, roughness), 0.0, 1.0) * 63.0;
    ivec2 i0 = ivec2(floor(texel));
    ivec2 i1 = min(i0 + 1, ivec2(63));
    vec2 f = texel - vec2(i0);
    vec3 a = mix(imageLoad(specLut, i0).xyz, imageLoad(specLut, ivec2(i1.x, i0.y)).xyz, f.x);
    vec3 b = mix(imageLoad(specLut, ivec2(i0.x, i1.y)).xyz, imageLoad(specLut, i1).xyz, f.x);
    return mix(a, b, f.y);
}
#endif

#endif
