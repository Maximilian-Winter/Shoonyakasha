#version 450
//
// The atmosphere over the scene, then exposure and tone mapping. For each
// pixel: the view ray from the camera, the distance to what the depth
// buffer holds there (reverse-Z: 0 is the sky), the scene seen through the
// atmosphere plus the light it scatters towards the camera
// (atmosphere.glsl), and the sun's disk where the ray reaches the sky.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "atmosphere.glsl"
#include "sk/tonemap.glsl"

layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D sceneDepth;

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

    // The swapchain is sRGB: writing to it encodes the linear colour.
    outColor = vec4(ACESFilm(colour * atmosphere.mie.w), 1.0);
}
