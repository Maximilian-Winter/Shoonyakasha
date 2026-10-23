// restir_spatial.glsl - spatial reservoir resampling, at half resolution,
// for pass SPATIAL_PASS (0 or 1). Each pixel merges its own reservoir and
// those of neighbours on a golden-angle spiral (8 in pass 0, 5 in pass 1),
// whose radius (32 to 12 pixels in pass 0, 16 to 6 in pass 1) narrows as
// the pixel's reservoir fills. A neighbour's sample is weighed by its
// luminance times the cosine from here, by how alike the neighbour's
// surface is (normal and depth), and by the square root of the jacobian of
// seeing its hit from here instead of from the neighbour. Pass 1 also
// marches the sample's direction through the depth buffer, for occlusion
// close to the surface.

#include "rtdgi_common.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 3, binding = 0, rg32ui) uniform readonly uimage2D reservoirIn;
layout(set = 3, binding = 1, rgba32f) uniform readonly image2D giPosition;
layout(set = 3, binding = 2, rgba16f) uniform readonly image2D giNormal;
layout(set = 3, binding = 3, rgba32f) uniform readonly image2D trHit;
layout(set = 3, binding = 4, rgba16f) uniform readonly image2D trNormal;
layout(set = 3, binding = 5, rg32ui) uniform writeonly uimage2D reservoirSpatialOut;

const float M_CLAMP = 20.0;
const float W_CLAMP = 10.0;

// Two thirds of SmeLU: lets surfaces facing somewhat away still share.
float normalInfluence(float x, float b) {
    return x < -b ? 0.0 : (x + b) * (x + b) / (4.0 * b);
}

// Visibility of a point along the march from `start` to `end` (world),
// against the full resolution depth: up to 6 steps, at least 2 half
// resolution pixels apart, each surface 5% of its depth thick.
float occlusionMarch(vec3 start, vec3 end) {
    vec3 a = worldToUvDepth(start), b = worldToUvDepth(end);
    if (b.z <= 0.0) return 1.0;
    float lengthPx = length((b.xy - a.xy) * camera.resolution * 0.5);
    int steps = min(6, int(floor(lengthPx / 2.0)));
    float visibility = 1.0;
    for (int k = 0; k < steps; ++k) {
        float t = (float(k) + 0.5) / float(steps);
        vec3 p = worldToUvDepth(mix(start, end, t));
        if (any(lessThan(p.xy, vec2(0.0))) || any(greaterThanEqual(p.xy, vec2(1.0)))) break;
        float screenDepth = imageLoad(giPosition, ivec2(p.xy * camera.resolution)).w;
        if (screenDepth > 0.0 && screenDepth < p.z * 0.99) {
            float difference = depthRelativeDiff(p.z, screenDepth);
            visibility *= 1.0 - smoothstep(0.05, 0.025, difference);
        }
    }
    return visibility;
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    ivec2 halfSize = imageSize(reservoirSpatialOut);
    if (any(greaterThanEqual(px, halfSize))) return;
    ivec2 fullSize = ivec2(camera.resolution);
    ivec2 hi = min(hiPixel(px), fullSize - 1);

    vec4 center = imageLoad(giPosition, hi);
    uvec2 centerRaw = imageLoad(reservoirIn, px).xy;
    if (center.w == 0.0 || (RTDGI_STAGES & STAGE_SPATIAL) == 0u) {
        imageStore(reservoirSpatialOut, px, uvec4(centerRaw, 0u, 0u));
        return;
    }
    vec3 centerNormal = imageLoad(giNormal, hi).xyz;
    vec3 centerNormalVs = directionWorldToView(centerNormal);
    uint rng = hashPixel(px, camera.frame + uint(SPATIAL_PASS) * 123u);

    Reservoir reservoir = Reservoir(0.0, 0u, 0.0, 0.0);
    ReservoirStream stream = ReservoirStream(0.0, 0.0);
    Reservoir centerR = reservoirFromRaw(centerRaw);

    // No ambient occlusion steers the kernel: it is as loose as the
    // reservoir's fullness allows.
    float maxInputM = SPATIAL_PASS == 0 ? M_CLAMP : M_CLAMP * 8.0;
    float tightness = 0.5 * smoothstep(maxInputM * 0.5, maxInputM, centerR.M);
    float maxRadius = SPATIAL_PASS == 0 ? mix(32.0, 12.0, tightness) : mix(16.0, 6.0, tightness);
    vec2 toEdge = min(vec2(px), vec2(halfSize - px));
    vec2 radius = min(vec2(maxRadius), toEdge * (centerR.M < 10.0 ? 100.0 : 1.25));
    uint sampleCount = SPATIAL_PASS == 0 ? 8u : 5u;

    float radiusOffset = randomU01(rng);
    uvec2 angleSeed = SPATIAL_PASS == 0 ? uvec2(px) >> 3u : uvec2(px) >> 2u;
    float angleOffset = float(hashPixel(ivec2(angleSeed), camera.frame * 2u + uint(SPATIAL_PASS)) >> 8) /
                        16777216.0 * TAU;

    for (uint i = 0u; i < sampleCount; ++i) {
        float angle = (float(i) + angleOffset) * GOLDEN_ANGLE;
        vec2 r = i == 0u ? vec2(0.0) : pow((float(i) + radiusOffset) / float(sampleCount), 0.5) * radius;
        ivec2 rpx = px + ivec2(vec2(cos(angle), sin(angle)) * r);
        if (any(lessThan(rpx, ivec2(0))) || any(greaterThanEqual(rpx, halfSize))) continue;
        bool isCenter = i == 0u;

        uvec2 raw = imageLoad(reservoirIn, rpx).xy;
        if (raw.x == 0u) continue;
        Reservoir n = reservoirFromRaw(raw);
        n.M = min(n.M, 500.0);
        ivec2 spx = payloadPixel(n.payload);
        if (any(greaterThanEqual(spx, halfSize))) continue;
        vec4 sampleHitLum = imageLoad(trHit, spx);
        vec3 sampleHit = sampleHitLum.xyz;
        vec3 sampleHitNormal = imageLoad(trNormal, spx).xyz;

        ivec2 rpxHi = min(hiPixel(rpx), fullSize - 1);
        vec4 neighbor = imageLoad(giPosition, rpxHi);
        if (neighbor.w == 0.0) continue;
        vec3 neighborNormalVs = directionWorldToView(imageLoad(giNormal, rpxHi).xyz);

        float relevance = normalInfluence(dot(neighborNormalVs, centerNormalVs), 0.5) / normalInfluence(1.0, 0.5);
        if (!isCenter) {
            float depthDiff = abs(max(0.3, centerNormalVs.z) * (neighbor.w / center.w - 1.0));
            relevance *= 1.0 - smoothstep(0.0, SPATIAL_PASS == 0 ? 0.15 : 0.1, depthDiff);
        }

        vec3 reusedToHit = sampleHit - neighbor.xyz;
        float reusedDistance = length(reusedToHit);
        vec3 reusedDir = reusedToHit / max(reusedDistance, 1e-8);
        vec3 toHit = sampleHit - center.xyz;
        float distanceToHit = length(toHit);
        vec3 dirToHit = toHit / max(distanceToHit, 1e-8);

        float visibility = 1.0;
        if (SPATIAL_PASS == 1) {
            float surfaceOffset = length(neighbor.xyz - center.xyz);
            vec3 end = center.xyz + toHit * min(1.0, 3.0 * surfaceOffset / max(distanceToHit, 1e-8));
            visibility = occlusionMarch(center.xyz, end);
        }

        float centerToHitVis = -dot(sampleHitNormal, dirToHit);
        float reusedToHitVis = -dot(sampleHitNormal, reusedDir);
        float p = sampleHitLum.w * max(0.0, dot(dirToHit, centerNormal));

        float jacobian = reusedDistance / max(distanceToHit, 1e-8);
        jacobian *= jacobian;
        jacobian *= clamp(centerToHitVis / max(reusedToHitVis, 1e-8), 0.0, 1e4);
        jacobian = isCenter ? 1.0 : sqrt(jacobian);

        if (!(p >= 0.0)) continue;
        n.M *= relevance;
        reservoirMerge(reservoir, n, p, visibility * jacobian, stream, n.payload, rng);
    }
    reservoirFinish(reservoir, stream);
    reservoir.W = min(reservoir.W, W_CLAMP);
    imageStore(reservoirSpatialOut, px, uvec4(reservoirRaw(reservoir), 0u, 0u));
}
