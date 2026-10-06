#version 450
// Averages the 4x4 pattern of gtao.frag away, across neighbours at a
// similar depth only, so occlusion does not bleed over silhouettes.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D aoRaw;
layout(set = 0, binding = 1) uniform sampler2D gDepth;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out float outAO;

float viewDepth(ivec2 p) {
    return -viewPositionFromDepth(camera.invProj, (vec2(p) + 0.5) / vec2(textureSize(gDepth, 0)),
                                  texelFetch(gDepth, p, 0).r).z;
}

void main() {
    ivec2 center = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(aoRaw, 0);
    float d0 = viewDepth(center);
    float sum = 0.0, weight = 0.0;
    for (int y = -2; y <= 1; ++y) {
        for (int x = -2; x <= 1; ++x) {
            ivec2 p = clamp(center + ivec2(x, y), ivec2(0), size - 1);
            float w = clamp(1.0 - abs(viewDepth(p) - d0) / (0.05 * d0 + 0.02), 0.0, 1.0);
            sum += texelFetch(aoRaw, p, 0).r * w;
            weight += w;
        }
    }
    outAO = weight > 0.0 ? sum / weight : texelFetch(aoRaw, center, 0).r;
}
