#version 460
// glslc: --target-env=vulkan1.2
// Ray-traced sun shadows: one ray per pixel towards a random point of the
// sun's disc, through the scene's acceleration structure. Over frames, TAA
// averages the rays into a soft shadow whose penumbra widens with distance
// from the caster, as a real one does, for each sun. Writes the same mask as
// shadow_mask.frag, which it replaces in the "raytraced" preset; needs a
// device with ray queries (the pass "requires" them).
//
// The acceleration structure holds the shadow casters, opaque and
// alpha-tested, skinned ones posed. The skinned ones also go into the
// cascaded shadow map (the preset keeps that pass and drops the others),
// and the mask is the product of both.

#extension GL_EXT_ray_query : require

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDepth;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform accelerationStructureEXT scene;
#define SK_SCENE_HITS_SET 2
#define SK_RAY_INSTANCES_BINDING 1
#define SK_SCENE_TEXTURES_BINDING 2
#include "sk/scene_hits.glsl"
layout(set = 3, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 3, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;
layout(set = 4, binding = 0) uniform sampler2DArrayShadow shadowMap;

#include "csm.glsl"
#include "rt_shadows.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outMask;

// Sun s's visibility at the pixel: one ray, times its cascades for the
// casters that are not in the acceleration structure. 1 without the sun.
float sunMask(int s, vec3 viewPos, vec3 worldPos, vec3 N) {
    if (sunCascadeCount(s) == 0) return 1.0;
    vec3 L = -normalize(sunTravel(s));
    if (dot(N, L) <= 0.0) return 0.0;   // facing away from the sun

    // A point on the sun's disc, different every pixel, frame and sun.
    float u = temporalNoise(gl_FragCoord.xy + 31.0 * float(s), camera.frame, camera.taa);
    float v = temporalNoise(gl_FragCoord.yx + 17.0 + 31.0 * float(s), camera.frame + 3u, camera.taa);
    bool shadowed = sunRayBlocked(worldPos + N * sunRayOffset(-viewPos.z), L, u, v);
    int cascade;
    float mapped = sunVisibility(s, worldPos, N, L, -viewPos.z, u, cascade);
    return shadowed ? 0.0 : mapped;
}

void main() {
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    if (depth >= 1.0) {
        outMask = vec4(1.0);
        return;
    }

    vec3 viewPos = viewPositionFromDepth(camera.invProj, fragTexCoord, depth);
    vec3 worldPos = (camera.invView * vec4(viewPos, 1.0)).xyz;
    vec3 N = octDecode(textureLod(gNormal, fragTexCoord, 0.0).xy);
    outMask = vec4(sunMask(0, viewPos, worldPos, N), 1.0, sunMask(1, viewPos, worldPos, N), 1.0);
}
