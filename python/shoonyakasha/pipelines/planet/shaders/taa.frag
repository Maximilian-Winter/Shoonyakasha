#version 450
//
// Temporal anti-aliasing of the scene's radiance (hdrColor) before the
// atmosphere goes over it. The geometry passes jitter their positions by a
// sub-pixel offset that changes every frame (camera.glsl's jittered), so
// the frames sample different points of each pixel; this pass blends each
// frame into the history (taaHistory, the frame before's result).
//
// A pixel's point is rebuilt from its depth (reverse-Z: 0 is the sky, taken
// as a direction) at the pixel's centre, moved by how far the
// camera moved since the frame before relative to the body under it
// (camera.taaMotion; points on that body keep their place), and projected
// with the frame before's projection and view (camera.prevViewProj) to find
// where it was then. The history there (historyCatmullRom) is clamped in
// YCoCg to the pixel's 3 x 3 neighbourhood: to its mean plus and minus
// TAA_CLIP standard deviations, within its least and greatest (stale
// colours cannot trail), then
// TAA_BLEND of this frame is blended in. Both are blended as c / (1 + luma)
// and turned back after, so a few very bright pixels do not dominate. The
// history is dropped (this frame taken alone) where it has never been
// written (alpha 0), where the point was off the screen, without temporal
// anti-aliasing (camera.taa.z), and when the world asks (camera.taa.w).
// The result's alpha is 1.
//

#define CAMERA_SET 0
#include "camera.glsl"

layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D sceneDepth;
layout(set = 1, binding = 2) uniform sampler2D taaHistory;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float TAA_BLEND = 0.1;
const float TAA_CLIP = 1.0;

float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 compress(vec3 c) {
    return c / (1.0 + luma(c));
}

vec3 expand(vec3 c) {
    return c / max(1.0 - luma(c), 1e-4);
}

// The history at `uv` filtered by a Catmull-Rom spline over the 4 x 4
// texels around it, from five bilinear taps (the corners' small weights
// left out, the middle 2 x 2 in one tap), which keeps it sharp where a
// bilinear tap would blur it a little more every frame the camera moves.
// Negative lobes can overshoot, so it is kept at or above 0. Its alpha is
// the bilinear tap's at `uv`.
vec4 historyCatmullRom(vec2 uv) {
    vec2 size = vec2(textureSize(taaHistory, 0));
    vec2 position = uv * size;
    vec2 centre = floor(position - 0.5) + 0.5;
    vec2 f = position - centre;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 at0 = (centre - 1.0) / size;
    vec2 at3 = (centre + 2.0) / size;
    vec2 at12 = (centre + w2 / w12) / size;
    vec3 sum = texture(taaHistory, vec2(at12.x, at0.y)).rgb * w12.x * w0.y
             + texture(taaHistory, vec2(at0.x, at12.y)).rgb * w0.x * w12.y
             + texture(taaHistory, vec2(at12.x, at12.y)).rgb * w12.x * w12.y
             + texture(taaHistory, vec2(at3.x, at12.y)).rgb * w3.x * w12.y
             + texture(taaHistory, vec2(at12.x, at3.y)).rgb * w12.x * w3.y;
    float total = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return vec4(max(sum / total, vec3(0.0)), texture(taaHistory, uv).a);
}

vec3 toYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 fromYCoCg(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

void main() {
    ivec2 size = textureSize(hdrColor, 0);
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec3 current = compress(texelFetch(hdrColor, texel, 0).rgb);
    if (camera.taa.z < 0.5 || camera.taa.w > 0.5) {
        outColor = vec4(expand(current), 1.0);
        return;
    }

    // The 3 x 3 neighbourhood's range in YCoCg.
    vec3 low = vec3(1e9), high = vec3(-1e9), sum = vec3(0.0), squares = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 at = clamp(texel + ivec2(x, y), ivec2(0), size - 1);
            vec3 c = toYCoCg(compress(texelFetch(hdrColor, at, 0).rgb));
            low = min(low, c);
            high = max(high, c);
            sum += c;
            squares += c * c;
        }
    }
    vec3 mean = sum / 9.0;
    vec3 spread = TAA_CLIP * sqrt(max(squares / 9.0 - mean * mean, vec3(0.0)));
    low = max(low, mean - spread);
    high = min(high, mean + spread);

    // Where the pixel's point was the frame before.
    vec2 ndc = fragTexCoord * 2.0 - 1.0;
    float depth = texelFetch(sceneDepth, texel, 0).r;
    vec4 viewPoint = camera.invProj * vec4(ndc, depth, 1.0);
    vec4 before;
    if (depth <= 0.0) {
        vec4 farPoint = camera.invProj * vec4(ndc, 1.0, 1.0);
        vec3 direction = mat3(camera.invView) * (farPoint.xyz / farPoint.w);
        before = camera.prevViewProj * vec4(direction, 0.0);
    } else {
        vec3 relative = mat3(camera.invView) * (viewPoint.xyz / viewPoint.w);
        before = camera.prevViewProj * vec4(relative + camera.taaMotion.xyz, 1.0);
    }
    vec2 uvBefore = (before.xy / before.w) * 0.5 + 0.5;
    if (before.w <= 0.0 || any(lessThan(uvBefore, vec2(0.0))) || any(greaterThan(uvBefore, vec2(1.0)))) {
        outColor = vec4(expand(current), 1.0);
        return;
    }
    vec4 history = historyCatmullRom(uvBefore);
    if (history.a < 0.5) {
        outColor = vec4(expand(current), 1.0);
        return;
    }
    vec3 clamped = fromYCoCg(clamp(toYCoCg(compress(history.rgb)), low, high));
    outColor = vec4(expand(mix(clamped, current, TAA_BLEND)), 1.0);
}
