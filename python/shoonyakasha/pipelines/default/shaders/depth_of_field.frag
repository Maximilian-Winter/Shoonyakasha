#version 450
// Depth of field (pass DepthOfField), as a camera with a full-frame sensor
// (24 mm high) and the view's focal length would show it at f-number
// settings.dofFStop, focused settings.dofFocus metres away. Each pixel
// gathers a disc of samples on a golden-angle spiral as wide as the largest
// blur; a sample counts where its own circle of confusion reaches back to
// this pixel, so a blurred foreground spreads over what is behind it while a
// blurred background does not spread over a sharp foreground (Dennis
// Gustafsson's single-pass gather). Reads motion blur's result and writes
// sceneColor, which bloom and the tone mapping read; with settings.dof 0, or
// a debug view, it copies.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D motionColor;
layout(set = 0, binding = 1) uniform sampler2D gDepth;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float SENSOR_HEIGHT = 0.024;      // metres: full frame
const float GOLDEN_ANGLE = 2.39996323;
const float MAX_BLUR = 0.02;            // largest blur radius, as a fraction of the image height
const float MAX_SAMPLES = 160.0;

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
    vec3 center = texelFetch(motionColor, ivec2(gl_FragCoord.xy), 0).rgb;
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
    float angle = 0.0;

    vec3 color = center;
    float total = 1.0;
    for (float radius = step; radius < maxRadius; radius += step / radius) {
        vec2 uv = fragTexCoord + vec2(cos(angle), sin(angle)) * texel * radius;
        angle += GOLDEN_ANGLE;
        float depth = viewDepth(uv);
        float sampleRadius = blurRadius(depth, focal, focus, maxRadius);
        // Behind this pixel, a sample spreads no further than this pixel's own blur.
        if (depth > centerDepth) sampleRadius = min(sampleRadius, centerRadius * 2.0);
        float weight = smoothstep(radius - 0.5, radius + 0.5, sampleRadius);
        color += mix(color / total, textureLod(motionColor, uv, 0.0).rgb, weight);
        total += 1.0;
    }
    outColor = vec4(color / total, 1.0);
}
