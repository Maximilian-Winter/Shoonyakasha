#version 450
// The sky where the depth prepass left the far plane, black elsewhere for
// the forward passes to shade over.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDepth;
layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube prefilterMap;
layout(set = 1, binding = 2) uniform sampler2D brdfLUT;
layout(set = 2, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 3, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    if (depth < 1.0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 viewPos = viewPositionFromDepth(camera.invProj, fragTexCoord, depth);
    vec3 dir = normalize(mat3(camera.invView) * viewPos);
    outColor = vec4(textureLod(prefilterMap, dir, settings.skyBlur).rgb * settings.iblIntensity, 1.0);
}
