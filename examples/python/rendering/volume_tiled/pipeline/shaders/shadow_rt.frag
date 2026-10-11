#version 460
// glslc: --target-env=vulkan1.2
// Ray-traced sun shadows: one ray per pixel towards a random point of the
// sun's disc, through the scene's acceleration structure. Over frames, TAA
// averages the rays into a soft shadow whose penumbra widens with distance
// from the caster, as a real one does. Writes the same mask as
// shadow_mask.frag, which it replaces in the "raytraced" preset; needs a
// device with ray queries (the pass "requires" them).
//
// The acceleration structure holds the static, opaque shadow casters. The
// rest, skinned and alpha-tested, still go into the cascaded shadow map
// (the preset keeps those passes and drops the opaque one), and the mask is
// the product of both.

#extension GL_EXT_ray_query : require

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDepth;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 3, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 3, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;
layout(set = 4, binding = 0) uniform sampler2DArrayShadow shadowMap;

#include "csm.glsl"
#include "rt_shadows.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec2 outMask;

void main() {
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    if (depth >= 1.0 || cascades.sunEnabled == 0u) {
        outMask = vec2(1.0, 1.0);
        return;
    }

    vec3 viewPos = viewPositionFromDepth(camera.invProj, fragTexCoord, depth);
    vec3 worldPos = (camera.invView * vec4(viewPos, 1.0)).xyz;
    vec3 N = octDecode(textureLod(gNormal, fragTexCoord, 0.0).xy);
    vec3 L = -normalize(cascades.sunDirection.xyz);
    if (dot(N, L) <= 0.0) {
        outMask = vec2(0.0, 1.0);   // facing away from the sun
        return;
    }

    // A point on the sun's disc, different every pixel and frame.
    float u = temporalNoise(gl_FragCoord.xy, camera.frame, camera.taa);
    float v = temporalNoise(gl_FragCoord.yx + 17.0, camera.frame + 3u, camera.taa);
    bool shadowed = sunRayBlocked(worldPos + N * sunRayOffset(-viewPos.z), L, u, v);

    // The casters that are not in the acceleration structure.
    int cascade;
    float mapped = sunVisibility(worldPos, N, L, -viewPos.z, u, cascade);
    outMask = vec2(shadowed ? 0.0 : mapped, 1.0);
}
