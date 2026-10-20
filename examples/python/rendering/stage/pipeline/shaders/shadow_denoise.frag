#version 450
// The light of the ray-shadowed lights, filtered and added to the HDR target
// (pass ShadowDenoise). Each pixel traced one shadow ray per light, so where
// a light is partly hidden the light that got past is noisy. This averages
// the share of the unshadowed light that got past (by luminance) over the
// neighbours on the same surface (near the pixel's plane, with a similar
// normal and nearly the same unshadowed light), keeps the average within the
// shares of the 5 x 5 pixels around, and gives the pixel its own unshadowed
// light times it. The radius follows the penumbra, so hard shadow edges stay
// sharp. TAA averages what is left over frames.
//
//   shadowLit    rgb  the light that got past the shadow rays
//                a    the narrowest penumbra of the blocked lights, in
//                     pixels; 0 where no ray was blocked
//   shadowTotal  rgb  the light unshadowed
//                a    1 where a ray-shadowed light reaches the surface

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D shadowLit;
layout(set = 0, binding = 1) uniform sampler2D shadowTotal;
layout(set = 0, binding = 2) uniform sampler2D gDepth;
layout(set = 0, binding = 3) uniform sampler2D gNormal;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// The filter's radius in pixels: RADIUS_PER_PENUMBRA times the penumbra, at
// most MAX_RADIUS. Pixels with no blocked ray take the penumbra from those
// within PENUMBRA_SEARCH pixels.
const float RADIUS_PER_PENUMBRA = 0.5;
const float MAX_RADIUS = 8.0;
const int PENUMBRA_SEARCH = 8;
// The averaged share stays within those of the pixels this close.
const int CLAMP_RADIUS = 2;
// How sharply a neighbour's weight falls as its unshadowed light differs
// from the pixel's: the ratio of the two, smaller over larger, to this power.
const float SIMILARITY_POWER = 64.0;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// The share of the unshadowed light that got past the rays, by luminance.
float litFraction(vec3 lit, vec3 total) {
    return clamp(luma(lit) / max(luma(total), 1e-8), 0.0, 1.0);
}

vec3 viewPositionAt(ivec2 p, vec2 size) {
    return viewPositionFromDepth(camera.invProj, (vec2(p) + 0.5) / size, texelFetch(gDepth, p, 0).r);
}

// The centre pixel: its view-space position, normal (world and view space),
// and how far a neighbour may lie off its plane and still count.
vec3 P, N, viewN;
float tolerance;

// 1 for a neighbour on the centre's plane with its normal, falling to 0 with
// the distance off the plane and the angle between the normals.
float sameSurface(ivec2 p, vec2 size) {
    float plane = abs(dot(viewN, viewPositionAt(p, size) - P));
    float facing = max(dot(N, octDecode(texelFetch(gNormal, p, 0).xy)), 0.0);
    return clamp(1.0 - plane / tolerance, 0.0, 1.0) * pow(facing, 64.0);
}

void main() {
    ivec2 center = ivec2(gl_FragCoord.xy);
    vec4 total = texelFetch(shadowTotal, center, 0);
    if (total.a == 0.0) {
        outColor = vec4(0.0);
        return;
    }
    vec4 lit = texelFetch(shadowLit, center, 0);
    ivec2 size = textureSize(shadowLit, 0);
    P = viewPositionAt(center, vec2(size));
    N = octDecode(texelFetch(gNormal, center, 0).xy);
    viewN = mat3(camera.view) * N;
    tolerance = 0.005 * -P.z + 0.003;

    // The pixel's own penumbra where a ray was blocked; elsewhere the
    // narrowest of the blocked pixels around on the same surface, from 3 x 3
    // taps over the search distance.
    float penumbra = lit.a;
    for (int y = -1; y <= 1 && lit.a == 0.0; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 p = clamp(center + ivec2(x, y) * PENUMBRA_SEARCH, ivec2(0), size - 1);
            float other = texelFetch(shadowLit, p, 0).a;
            if (other > 0.0 && sameSurface(p, vec2(size)) > 0.5) {
                penumbra = penumbra > 0.0 ? min(penumbra, other) : other;
            }
        }
    }
    float radius = min(penumbra * RADIUS_PER_PENUMBRA, MAX_RADIUS);
    if (radius < 0.5) {
        outColor = vec4(lit.rgb, 0.0);
        return;
    }
    float ownLuma = luma(total.rgb);

    // 5 x 5 taps spread over the radius, Gaussian-weighted.
    float spacing = radius * 0.5;
    float sum = 0.0;
    float weights = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            ivec2 p = center + ivec2(round(vec2(x, y) * spacing));
            if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) continue;
            vec4 t = texelFetch(shadowTotal, p, 0);
            if (t.a == 0.0) continue;
            float tLuma = luma(t.rgb);
            float similar = (min(tLuma, ownLuma) + 1e-3) / (max(tLuma, ownLuma) + 1e-3);
            float w = exp(-0.3 * float(x * x + y * y)) * sameSurface(p, vec2(size))
                    * pow(similar, SIMILARITY_POWER);
            sum += litFraction(texelFetch(shadowLit, p, 0).rgb, t.rgb) * w;
            weights += w;
        }
    }
    if (weights <= 0.0) {
        outColor = vec4(lit.rgb, 0.0);
        return;
    }

    // Kept within the shares of the 5 x 5 pixels around: inside a hard
    // shadow, a few pixels from its edge, all of them are 0.
    float lo = 1.0, hi = 0.0;
    for (int y = -CLAMP_RADIUS; y <= CLAMP_RADIUS; ++y) {
        for (int x = -CLAMP_RADIUS; x <= CLAMP_RADIUS; ++x) {
            ivec2 p = clamp(center + ivec2(x, y), ivec2(0), size - 1);
            vec4 t = texelFetch(shadowTotal, p, 0);
            if (t.a == 0.0) continue;
            float f = litFraction(texelFetch(shadowLit, p, 0).rgb, t.rgb);
            lo = min(lo, f);
            hi = max(hi, f);
        }
    }
    outColor = vec4(total.rgb * clamp(sum / weights, lo, hi), 0.0);
}
