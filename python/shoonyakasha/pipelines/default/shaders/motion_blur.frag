#version 450
// Motion blur (pass MotionBlur): TAA's result smeared along each pixel's
// motion over the part of the frame the shutter is open,
// settings.motionBlur (0.5 is a 180 degree shutter). The motion is gVelocity
// (where the surface was last frame), and for the sky, last frame's camera
// direction. Each pixel averages MOTION_TAPS samples across its own
// motion, centred on it; a sample nearer the camera than the pixel counts
// only where its own blur reaches this far, so a moving object does not
// smear over a still one in front of it. Writes motionColor, which depth of
// field reads; with settings.motionBlur 0 or a debug view, a copy.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D taaColor;
layout(set = 0, binding = 1) uniform sampler2D gDepth;
layout(set = 0, binding = 2) uniform sampler2D gVelocity;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const int MOTION_TAPS = 16;
const float MAX_BLUR = 0.05;   // longest blur, as a fraction of the image width

// The screen motion at uv over the open shutter, in UV units: where it is
// minus where it was.
vec2 blurAt(vec2 uv, float depth) {
    vec2 motion;
    if (depth < 1.0) {
        motion = textureLod(gVelocity, uv, 0.0).xy;
    } else {
        vec3 viewPos = viewPositionFromDepth(camera.invProj, uv, depth);
        vec4 prevClip = camera.prevViewProj * vec4(mat3(camera.invView) * viewPos, 0.0);
        motion = uv - (prevClip.xy / prevClip.w * 0.5 + 0.5);
    }
    motion *= clamp(settings.motionBlur, 0.0, 1.0);
    float pixels = length(motion * camera.resolution);
    float longest = MAX_BLUR * camera.resolution.x;
    return pixels > longest ? motion * (longest / pixels) : motion;
}

float viewDepth(vec2 uv, float depth) {
    return -viewPositionFromDepth(camera.invProj, uv, depth).z;
}

void main() {
    vec3 center = texelFetch(taaColor, ivec2(gl_FragCoord.xy), 0).rgb;
    if (settings.motionBlur <= 0.0 || settings.debugView != 0u) {
        outColor = vec4(center, 1.0);
        return;
    }
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    vec2 blur = blurAt(fragTexCoord, depth);
    vec2 pixels = blur * camera.resolution;
    if (dot(pixels, pixels) < 0.25) {
        outColor = vec4(center, 1.0);
        return;
    }

    float centerDepth = viewDepth(fragTexCoord, min(depth, 0.999999));
    float jitter = temporalNoise(gl_FragCoord.xy, camera.frame, camera.taa) - 0.5;
    vec3 sum = center;
    float total = 1.0;
    for (int i = 0; i < MOTION_TAPS; ++i) {
        float t = (float(i) + 0.5 + jitter) / float(MOTION_TAPS) - 0.5;   // -0.5..0.5 across the blur
        vec2 uv = fragTexCoord + blur * t;
        float sampleDepth = textureLod(gDepth, uv, 0.0).r;
        float weight = 1.0;
        if (viewDepth(uv, min(sampleDepth, 0.999999)) < centerDepth * 0.98) {
            // In front: only where its own blur reaches back to this pixel.
            vec2 own = blurAt(uv, sampleDepth) * camera.resolution;
            float reach = 0.5 * length(own);
            weight = clamp(reach - abs(t) * length(pixels) + 0.5, 0.0, 1.0);
        }
        sum += textureLod(taaColor, uv, 0.0).rgb * weight;
        total += weight;
    }
    outColor = vec4(sum / total, 1.0);
}
