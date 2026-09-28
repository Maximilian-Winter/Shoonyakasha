#version 450
// Sun shadow mask: how much of the sun reaches each visible pixel, from the
// cascaded shadow map and a short screen-space march towards the sun that
// catches contact shadows smaller than a shadow texel.
//
//   r  visibility, 0 = fully shadowed
//   g  cascade used / 4, 1 past the last cascade (for the cascade debug view)

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDepth;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform sampler2DArrayShadow shadowMap;
layout(set = 3, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 3, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;

#include "csm.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec2 outMask;

// 0 where something on screen sits between viewPos and the sun, within
// `contactShadowThickness` behind the depth buffer's surface there.
float contactShadow(vec3 viewPos, vec3 viewN, vec3 viewL, float noise) {
    float len = settings.contactShadowLength;
    if (len <= 0.0) return 1.0;

    // The world size of a pixel here. The march starts two of them off the
    // surface and ignores anything closer than one, so the surface it starts
    // from cannot shadow itself where the sun grazes it.
    vec2 size = vec2(textureSize(gDepth, 0));
    float pixel = 2.0 * -viewPos.z / (abs(camera.proj[1][1]) * size.y);

    const int STEPS = 12;
    vec3 start = viewPos + viewN * (2.0 * pixel);
    vec3 step = viewL * (len / float(STEPS));
    vec3 p = start + step * noise;
    for (int i = 0; i < STEPS; ++i, p += step) {
        vec4 clip = camera.proj * vec4(p, 1.0);
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
        // Reconstruct at the texel's centre: its depth belongs there, and
        // anywhere else a sloped surface would seem to rise and fall.
        uv = (floor(uv * size) + 0.5) / size;
        float depth = textureLod(gDepth, uv, 0.0).r;
        if (depth >= 1.0) continue;
        float sceneZ = viewPositionFromDepth(camera.invProj, uv, depth).z;
        float behind = sceneZ - p.z;   // > 0: the surface on screen is nearer the camera than p
        if (behind > pixel && behind < settings.contactShadowThickness) {
            // Fade hits near the end of the ray, so the shadow has no hard tip.
            return float(i) / float(STEPS);
        }
    }
    return 1.0;
}

void main() {
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    if (depth >= 1.0) {
        outMask = vec2(1.0, 1.0);
        return;
    }

    vec3 viewPos = viewPositionFromDepth(camera.invProj, fragTexCoord, depth);
    vec3 worldPos = (camera.invView * vec4(viewPos, 1.0)).xyz;
    vec3 N = octDecode(textureLod(gNormal, fragTexCoord, 0.0).xy);
    vec3 L = -normalize(cascades.sunDirection.xyz);
    float noise = temporalNoise(gl_FragCoord.xy, camera.frame, camera.taa);

    int cascade;
    float visibility = sunVisibility(worldPos, N, L, -viewPos.z, noise, cascade);

    if (visibility > 0.0 && cascades.sunEnabled != 0u && dot(N, L) > 0.0) {
        mat3 toView = mat3(camera.view);
        visibility *= contactShadow(viewPos, toView * N, toView * L, noise);
    }
    outMask = vec2(visibility, cascade < 0 ? 1.0 : float(cascade) / 4.0);
}
