// area_lights.glsl - rectangular lights by linearly transformed cosines
// (Heitz, Dupuy, Hill and Neubelt 2016). Included by lights.glsl. The
// including shader declares `brdfLUT` and the fitted table, ltc.bin, as
//
//     layout(std430, set = <lightsSet>, binding = 4) readonly buffer LtcTable { vec4 ltcTable[]; };
//
// GGX's lobe for one roughness and view angle is close to a clamped cosine
// stretched by a 3x3 matrix, and a cosine's integral over a polygon has a
// closed form. So the rectangle's corners go through the inverse matrix from
// the table, and the integral there is the share of the light the BRDF
// reflects towards the viewer. The table holds only the lobe's shape; its
// scale and Fresnel come from brdfLUT, which integrates the same BRDF
// (ltc_fit.py fits the pipeline's GGX).

#ifndef SHOWROOM_AREA_LIGHTS_GLSL
#define SHOWROOM_AREA_LIGHTS_GLSL

const int LTC_SIZE = 64;

vec4 ltcFetch(ivec2 p) {
    p = clamp(p, ivec2(0), ivec2(LTC_SIZE - 1));
    return ltcTable[p.y * LTC_SIZE + p.x];
}

// The inverse matrix for a roughness and NdotV, filtered between the four
// nearest fits. Across: roughness; down: sqrt(1 - NdotV).
mat3 ltcInverse(float roughness, float NdotV) {
    vec2 uv = vec2(roughness, sqrt(1.0 - NdotV)) * float(LTC_SIZE - 1);
    ivec2 p = ivec2(floor(uv));
    vec2 f = uv - vec2(p);
    vec4 t = mix(mix(ltcFetch(p), ltcFetch(p + ivec2(1, 0)), f.x),
                 mix(ltcFetch(p + ivec2(0, 1)), ltcFetch(p + ivec2(1, 1)), f.x), f.y);
    return mat3(vec3(t.x, 0.0, t.y), vec3(0.0, 1.0, 0.0), vec3(t.z, 0.0, t.w));
}

// The edge's share of the integral: the normal of the plane through both
// directions times their angle over 2 pi, the angle from a fit that keeps
// precision where acos would lose it (Hill and Heitz 2017).
vec3 ltcEdge(vec3 a, vec3 b) {
    float x = dot(a, b);
    float y = abs(x);
    float p = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float q = 3.4175940 + (4.1616724 + y) * y;
    float v = p / q;
    float thetaOverSin = x > 0.0 ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(a, b) * thetaOverSin;
}

// The integral over the rectangle (corners relative to the shading point) of
// the cosine `toCosine` makes of the BRDF's lobe: 0 to 1, the form factor in
// the cosine's space. `frame` turns world directions into the shading frame
// (x towards V, z the normal).
float ltcRectangle(mat3 toCosine, vec3 corners[4]) {
    vec3 p[4];
    for (int k = 0; k < 4; ++k) p[k] = toCosine * corners[k];

    // What lies below the horizon does not reach the surface: clip it away.
    // A convex quad cut by a plane has at most five corners.
    vec3 clipped[5];
    int n = 0;
    for (int k = 0; k < 4; ++k) {
        vec3 a = p[k];
        vec3 b = p[(k + 1) & 3];
        if (a.z >= 0.0 && n < 5) clipped[n++] = a;
        if ((a.z >= 0.0) != (b.z >= 0.0) && n < 5) clipped[n++] = mix(a, b, a.z / (a.z - b.z));
    }
    if (n < 3) return 0.0;

    float sum = 0.0;
    vec3 first = normalize(clipped[0]);
    vec3 a = first;
    for (int k = 1; k < n; ++k) {
        vec3 b = normalize(clipped[k]);
        sum += ltcEdge(a, b).z;
        a = b;
    }
    sum += ltcEdge(a, first).z;
    // Either winding: only the side facing the light is lit (lightFrom checks).
    return abs(sum);
}

// The shading frame: x towards the viewer in the surface's plane, z the
// normal, as the table was fitted.
mat3 ltcFrame(vec3 N, vec3 V) {
    vec3 T1 = V - N * dot(V, N);
    float len = length(T1);
    T1 = len > 1e-5 ? T1 / len : normalize(cross(N, abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 T2 = cross(N, T1);
    return transpose(mat3(T1, T2, N));
}

// Light reflected from a rectangle of radiance 1 (centre and half-axes
// relative to the shading point): diffuse plus specular, and the clear coat
// over them (coatFactor, coatRoughness, from lights.glsl).
vec3 rectangleBRDF(vec3 N, vec3 V, vec3 centre, vec3 halfX, vec3 halfY, vec3 albedo, float metallic,
                   float roughness, vec3 F0) {
    vec3 corners[4] = vec3[](centre - halfX - halfY, centre + halfX - halfY,
                             centre + halfX + halfY, centre - halfX + halfY);
    float NdotV = clamp(dot(N, V), 1e-4, 1.0);
    mat3 frame = ltcFrame(N, V);

    vec2 env = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 specularAlbedo = F0 * env.x + env.y;
    float diffuse = ltcRectangle(frame, corners);
    float specular = ltcRectangle(ltcInverse(roughness, NdotV) * frame, corners);
    vec3 result = (1.0 - specularAlbedo) * (1.0 - metallic) * albedo * diffuse + specularAlbedo * specular;

    if (coatFactor > 0.0) {
        vec2 coatEnv = texture(brdfLUT, vec2(NdotV, coatRoughness)).rg;
        float coat = ltcRectangle(ltcInverse(coatRoughness, NdotV) * frame, corners);
        result = result * (1.0 - coatFresnel(NdotV) * coatFactor)
               + vec3((COAT_F0 * coatEnv.x + coatEnv.y) * coatFactor * coat);
    }
    return result;
}

#endif
