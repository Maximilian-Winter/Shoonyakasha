#version 450
//
// The clouds, at a quarter of the screen's resolution each way: for each
// pixel the view ray from the camera is marched through the cloud layer
// (clouds.glsl) up to what the depth buffer holds at the pixel's centre
// (reverse-Z: 0 is the sky). Writes the light the clouds send towards the
// camera and their transmittance (cloudLayer), and their mean distance and
// the scene's distance marched to (cloudDistance), which the atmosphere
// pass uses to put the clouds together with the scene and the air.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneDepth;

layout(set = 1, binding = 1) uniform CloudParams {
    vec4 layer;
    vec4 shape;
    vec4 wind;
} clouds;

#include "clouds.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outCloud;
layout(location = 1) out vec2 outDistance;   // x: the clouds' mean distance; y: the scene's distance marched to

// A value from 0 to 1, without pattern, from pixel to pixel and frame to
// frame (a PCG hash of the pixel and the time).
float jitter(vec2 pixel) {
    uvec3 v = uvec3(uvec2(pixel), uint(camera.time * 60.0));
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return float(v.x & 0xFFFFFFu) / 16777216.0;
}

void main() {
    if (clouds.shape.w == 0.0) {
        outCloud = vec4(0.0, 0.0, 0.0, 1.0);
        outDistance = vec2(0.0);
        return;
    }
    vec2 ndc = fragTexCoord * 2.0 - 1.0;
    float depth = texture(sceneDepth, fragTexCoord).r;
    vec4 near = camera.invProj * vec4(ndc, 1.0, 1.0);
    vec3 rd = normalize(mat3(camera.invView) * (near.xyz / near.w));
    float sceneDistance = 1e30;
    if (depth > 0.0) {
        vec4 viewPos = camera.invProj * vec4(ndc, depth, 1.0);
        sceneDistance = length(viewPos.xyz / viewPos.w);
    }
    float distance;
    outCloud = marchClouds(camera.cameraFromCentre.xyz, rd, sceneDistance, jitter(gl_FragCoord.xy), distance);
    outDistance = vec2(distance, min(sceneDistance, 1e9));
}
