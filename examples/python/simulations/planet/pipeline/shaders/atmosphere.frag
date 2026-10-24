#version 450
//
// The atmosphere over the scene, then exposure and tone mapping. For each
// pixel: the view ray from the camera, the distance to what the depth
// buffer holds there (reverse-Z: 0 is the sky), the scene seen through the
// atmosphere plus the light it scatters towards the camera
// (atmosphere.glsl), and the sun's disk where the ray reaches the sky; then
// the clouds over it.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "atmosphere.glsl"
#include "sk/tonemap.glsl"

layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D sceneDepth;
layout(set = 2, binding = 0) uniform sampler2D cloudLayer;      // rgb: the clouds' light; a: their transmittance
layout(set = 2, binding = 1) uniform sampler2D cloudDistance;   // x: their mean distance (m); y: the scene's distance

// The clouds at this pixel from their lower-resolution images: the four
// nearest texels, weighted bilinearly and by how close the scene distance
// each was marched to is to this pixel's, so clouds do not bleed across
// the edges of nearer things. Returns the clouds' light and transmittance;
// `distance` their mean distance.
vec4 upsampledClouds(vec2 uv, float sceneDistance, out float distance) {
    ivec2 size = textureSize(cloudLayer, 0);
    vec2 coord = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(coord));
    vec2 f = coord - vec2(base);
    float here = min(sceneDistance, 1e9);
    vec4 cloud = vec4(0.0);
    float mean = 0.0, total = 0.0;
    for (int y = 0; y <= 1; ++y) {
        for (int x = 0; x <= 1; ++x) {
            ivec2 texel = clamp(base + ivec2(x, y), ivec2(0), size - 1);
            vec2 d = texelFetch(cloudDistance, texel, 0).xy;
            float bilinear = (x == 1 ? f.x : 1.0 - f.x) * (y == 1 ? f.y : 1.0 - f.y);
            float w = bilinear / (1e-3 + abs(d.y - here) / max(here, 1.0)) + 1e-6;
            cloud += texelFetch(cloudLayer, texel, 0) * w;
            mean += d.x * w;
            total += w;
        }
    }
    distance = mean / total;
    return cloud / total;
}

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

const float SUN_COS_INNER = 0.99996;   // a disk about 1 degree across
const float SUN_COS_OUTER = 0.99990;
const float SUN_DISK = 40.0;           // the disk's brightness, times the sun's illuminance

void main() {
    vec2 ndc = fragTexCoord * 2.0 - 1.0;
    float depth = texture(sceneDepth, fragTexCoord).r;
    vec4 near = camera.invProj * vec4(ndc, 1.0, 1.0);
    vec3 rd = normalize(mat3(camera.invView) * (near.xyz / near.w));

    float sceneDistance = 1e30;
    if (depth > 0.0) {
        vec4 viewPos = camera.invProj * vec4(ndc, depth, 1.0);
        sceneDistance = length(viewPos.xyz / viewPos.w);
    }

    vec3 ro = camera.cameraFromCentre.xyz;
    vec3 inscatter, transmittance;
    atmosphereAlong(ro, rd, sceneDistance, inscatter, transmittance);

    vec3 colour = texture(hdrColor, fragTexCoord).rgb;
    if (depth == 0.0) {
        float mu = dot(rd, atmosphere.sun.xyz);
        colour = atmosphere.mie.z * SUN_DISK * smoothstep(SUN_COS_OUTER, SUN_COS_INNER, mu) * vec3(1.0);
    }
    colour = colour * transmittance + inscatter;

    // The clouds (clouds.frag, at lower resolution): the air in front of
    // them, their light seen through it, and what lies behind them through
    // them.
    float cloudAt;
    vec4 cloud = upsampledClouds(fragTexCoord, sceneDistance, cloudAt);
    if (cloud.a < 0.999) {
        cloudAt = min(cloudAt, sceneDistance);
        vec3 inFront, throughFront;
        atmosphereAlong(ro, rd, cloudAt, inFront, throughFront);
        colour = inFront + throughFront * cloud.rgb + cloud.a * (colour - inFront);
    }

    // The swapchain is sRGB: writing to it encodes the linear colour.
    outColor = vec4(ACESFilm(colour * atmosphere.mie.w), 1.0);
}
