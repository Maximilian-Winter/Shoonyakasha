#version 450
// Depth of field, as a camera with a full-frame sensor (24 mm high) and the
// view's focal length would show it at f-number settings.dofFStop, focused
// settings.dofFocus metres away. Each pixel gathers a disc of samples on a
// golden-angle spiral as wide as the largest blur; a sample counts where its
// own circle of confusion reaches back to this pixel, so a blurred
// foreground spreads over what is behind it while a blurred background does
// not spread over a sharp foreground (Dennis Gustafsson's single-pass
// gather).
//
// Also picks this frame's colour for the passes after it: the raw frame
// while capture mode averages raw frames, TAA's result otherwise. With depth
// of field off it only copies that.
//
// While capture mode averages, the spiral turns a different way per pixel
// and frame, so the average has no pattern from the sample positions.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D rawColor;
layout(set = 0, binding = 1) uniform sampler2D taaColor;
layout(set = 0, binding = 2) uniform sampler2D gDepth;
layout(set = 0, binding = 3) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float SENSOR_HEIGHT = 0.024;      // metres: full frame
const float GOLDEN_ANGLE = 2.39996323;
const float MAX_BLUR = 0.02;            // largest blur radius, as a fraction of the image height
const float MAX_SAMPLES = 160.0;

bool useRaw() { return settings.accumulate != 0u && settings.accumSource == 1u; }

vec3 colorAt(vec2 uv) {
    return useRaw() ? textureLod(rawColor, uv, 0.0).rgb : textureLod(taaColor, uv, 0.0).rgb;
}

// Distance along the view axis, in metres; far for the sky.
float viewDepth(vec2 uv) {
    float d = textureLod(gDepth, uv, 0.0).r;
    vec4 v = camera.invProj * vec4(uv * 2.0 - 1.0, d, 1.0);
    return abs(v.w) > 1e-7 ? min(abs(v.z / v.w), 1e5) : 1e5;
}

// Radius of the circle of confusion in pixels, at most maxRadius.
float blurRadius(float depth, float focal, float focus, float maxRadius) {
    float aperture = focal / max(settings.dofFStop, 0.5);
    float c = aperture * focal * abs(depth - focus) / (depth * (focus - focal));   // diameter on the sensor
    return min(0.5 * c / SENSOR_HEIGHT * camera.resolution.y, maxRadius);
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 center = useRaw() ? texelFetch(rawColor, pixel, 0).rgb : texelFetch(taaColor, pixel, 0).rgb;
    if (settings.dof == 0u || settings.debugView != 0u) {
        outColor = vec4(center, 1.0);
        return;
    }

    float focal = 0.5 * SENSOR_HEIGHT * abs(camera.proj[1][1]);      // from the vertical field of view
    float focus = max(settings.dofFocus, focal * 1.05);
    float maxRadius = MAX_BLUR * camera.resolution.y;
    vec2 texel = 1.0 / camera.resolution;

    float centerDepth = viewDepth(fragTexCoord);
    float centerRadius = blurRadius(centerDepth, focal, focus, maxRadius);

    // The radius grows by step/radius per sample, so the samples cover the
    // disc evenly: about maxRadius^2 / (2 step) of them.
    float step = clamp(maxRadius * maxRadius / (2.0 * MAX_SAMPLES), 0.5, 4.0);
    float angle = settings.accumulate != 0u
        ? 6.2831853 * interleavedGradientNoise(gl_FragCoord.xy + 5.588238 * float(settings.frame % 64u))
        : 0.0;

    vec3 color = center;
    float total = 1.0;
    for (float radius = step; radius < maxRadius; radius += step / radius) {
        vec2 uv = fragTexCoord + vec2(cos(angle), sin(angle)) * texel * radius;
        angle += GOLDEN_ANGLE;
        float depth = viewDepth(uv);
        float sampleRadius = blurRadius(depth, focal, focus, maxRadius);
        // Behind this pixel, a sample spreads no further than this pixel's own blur
        if (depth > centerDepth) sampleRadius = min(sampleRadius, centerRadius * 2.0);
        float weight = smoothstep(radius - 0.5, radius + 0.5, sampleRadius);
        color += mix(color / total, colorAt(uv), weight);
        total += 1.0;
    }
    outColor = vec4(color / total, 1.0);
}
