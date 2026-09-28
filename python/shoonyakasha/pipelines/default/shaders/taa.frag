#version 450
// Temporal anti-aliasing. The camera is jittered by a sub-pixel amount every
// frame (common.glsl taaJitter), so successive frames sample different parts
// of each pixel; this pass blends the new frame into last frame's result,
// found by reprojecting each pixel's position through last frame's camera.
//
// History is read with a Catmull-Rom filter, which stays sharp under
// repeated resampling, and clipped to the spread of the new frame's colours
// around the pixel (in YCoCg), so what was there before but is not any more
// cannot linger. Only the camera's motion is reprojected: moving objects rely
// on the clipping.
//
// The history image is persistent and starts cleared; its alpha is 1 once
// written, so alpha 0 means "no history": after a resize, for instance.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D current;
layout(set = 0, binding = 1) uniform sampler2D gDepth;
layout(set = 0, binding = 2) uniform sampler2D history;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float FEEDBACK = 0.9;   // how much of the history each frame keeps

vec3 toYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
vec3 fromYCoCg(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Catmull-Rom from 9 bilinear taps (Jimenez, "Filmic SMAA").
vec4 sampleCatmullRom(sampler2D tex, vec2 uv) {
    vec2 size = vec2(textureSize(tex, 0));
    vec2 position = uv * size;
    vec2 center = floor(position - 0.5) + 0.5;
    vec2 f = position - center;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 tc0 = (center - 1.0) / size, tc3 = (center + 2.0) / size;
    vec2 tc12 = (center + w2 / w12) / size;
    vec4 result =
        textureLod(tex, vec2(tc0.x,  tc0.y), 0.0) * (w0.x  * w0.y) +
        textureLod(tex, vec2(tc12.x, tc0.y), 0.0) * (w12.x * w0.y) +
        textureLod(tex, vec2(tc3.x,  tc0.y), 0.0) * (w3.x  * w0.y) +
        textureLod(tex, vec2(tc0.x,  tc12.y), 0.0) * (w0.x  * w12.y) +
        textureLod(tex, vec2(tc12.x, tc12.y), 0.0) * (w12.x * w12.y) +
        textureLod(tex, vec2(tc3.x,  tc12.y), 0.0) * (w3.x  * w12.y) +
        textureLod(tex, vec2(tc0.x,  tc3.y), 0.0) * (w0.x  * w3.y) +
        textureLod(tex, vec2(tc12.x, tc3.y), 0.0) * (w12.x * w3.y) +
        textureLod(tex, vec2(tc3.x,  tc3.y), 0.0) * (w3.x  * w3.y);
    return max(result, vec4(0.0));
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(current, 0);
    vec3 color = texelFetch(current, pixel, 0).rgb;
    if (camera.taa == 0u) {
        outColor = vec4(color, 1.0);
        return;
    }

    // The new frame's neighbourhood, and the nearest depth in it: at an
    // edge the foreground's motion is the one that matters.
    vec3 mean = vec3(0.0), meanSquare = vec3(0.0);
    float nearest = 1.0;
    ivec2 nearestPixel = pixel;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 p = clamp(pixel + ivec2(x, y), ivec2(0), size - 1);
            vec3 c = toYCoCg(texelFetch(current, p, 0).rgb);
            mean += c;
            meanSquare += c * c;
            float d = texelFetch(gDepth, p, 0).r;
            if (d < nearest) { nearest = d; nearestPixel = p; }
        }
    }
    mean /= 9.0;
    vec3 sigma = sqrt(max(meanSquare / 9.0 - mean * mean, vec3(0.0)));

    // Where this pixel was last frame. The sky reprojects as seen from the
    // camera's position, a direction only.
    vec2 uv = (vec2(nearestPixel) + 0.5) / vec2(size);
    vec3 viewPos = viewPositionFromDepth(camera.invProj, uv, nearest);
    vec4 world = camera.invView * vec4(viewPos, 1.0);
    if (nearest >= 1.0) world = vec4(mat3(camera.invView) * viewPos, 0.0);
    vec4 prevClip = camera.prevViewProj * world;
    vec2 prevUV = fragTexCoord + (prevClip.xy / prevClip.w * 0.5 + 0.5 - uv);

    vec4 previous = sampleCatmullRom(history, prevUV);
    bool offscreen = any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)));
    if (offscreen || previous.a < 0.5 || prevClip.w <= 0.0) {
        outColor = vec4(color, 1.0);
        return;
    }

    // Clip the history towards the neighbourhood's mean, to within one
    // standard deviation of it on each axis.
    vec3 h = toYCoCg(previous.rgb);
    vec3 extent = sigma * 1.0 + 1e-4;
    vec3 offset = h - mean;
    vec3 units = abs(offset / extent);
    float largest = max(units.x, max(units.y, units.z));
    if (largest > 1.0) h = mean + offset / largest;
    vec3 clipped = fromYCoCg(h);

    // Blend, weighting by inverse luminance so one bright sample cannot
    // flicker through (Karis 2014).
    float wCurrent = (1.0 - FEEDBACK) / (1.0 + luma(color));
    float wHistory = FEEDBACK / (1.0 + luma(clipped));
    outColor = vec4((color * wCurrent + clipped * wHistory) / (wCurrent + wHistory), 1.0);
}
