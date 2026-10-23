#version 450
// Height fog over the opaque scene and the sky (pass HeightFog); see
// fog.glsl. Blends the fog's light over the HDR target by the fog's share
// along each pixel's view ray, to the far plane for the sky. Nothing with
// settings.fogDensity 0 or a debug view.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDepth;
layout(set = 1, binding = 1) uniform samplerCube prefilterMap;
layout(set = 2, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 3, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;

#include "fog.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    if (settings.fogDensity <= 0.0 || settings.debugView != 0u) discard;
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    vec3 viewPos = viewPositionFromDepth(camera.invProj, fragTexCoord, min(depth, 0.999999));
    vec3 dir = normalize(mat3(camera.invView) * viewPos);
    float distance = depth >= 1.0 ? camera.params.y : length(viewPos);
    outColor = vec4(fogLight(dir), fogAmount(camera.position.xyz, dir, distance));
}
