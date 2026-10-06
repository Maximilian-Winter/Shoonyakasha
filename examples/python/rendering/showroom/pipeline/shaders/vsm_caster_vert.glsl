// vsm_caster_vert.glsl - places a shadow caster in one clipmap level of the
// virtual shadow map: the level's 32 x 32 page window fills the viewport, so
// gl_FragCoord is the texel within the window. Built as vsm.vert,
// vsm_masked.vert and vsm_skinned.vert; the pass instance picks the level
// through pass.repeatIndex.
//
// When the level has no page to draw this frame, every vertex is put outside
// the view, so its triangles are dropped before rasterising. (A renderer with
// indirect draws would skip the draws instead.)

#include "common.glsl"

layout(set = 0, binding = 2) uniform Showroom { SHOWROOM_BLOCK } showroom;
layout(set = 1, binding = 7, r32ui) uniform readonly uimage2D vsmLevelInfo;

#include "vsm.glsl"

#ifdef MASKED
layout(push_constant) uniform MaskedShadowDraw {
    mat4 model;
    vec4 baseColorFactor;
    uint cascade;
    float alphaCutoff;
} draw;
layout(location = 2) in vec2 inTexCoord;
layout(location = 2) out vec2 fragTexCoord;
#else
layout(push_constant) uniform ShadowDraw {
    mat4 model;
    uint cascade;
} draw;
#endif

#ifdef SKINNED
layout(std430, set = 2, binding = 0) readonly buffer BoneMatrices { mat4 bones[]; };
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 0) out float fragDepth;
layout(location = 1) flat out ivec3 fragLevelOrigin;   // level, window origin

void main() {
    int level = int(draw.cascade);
    if (imageLoad(vsmLevelInfo, ivec2(level, 0)).r == 0u) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        fragDepth = 1.0;
        fragLevelOrigin = ivec3(level, 0, 0);
#ifdef MASKED
        fragTexCoord = vec2(0.0);
#endif
        return;
    }

#ifdef SKINNED
    mat4 skin = inWeights.x * bones[inJoints.x] + inWeights.y * bones[inJoints.y] +
                inWeights.z * bones[inJoints.z] + inWeights.w * bones[inJoints.w];
    vec3 world = (draw.model * skin * vec4(inPosition, 1.0)).xyz;
#else
    vec3 world = (draw.model * vec4(inPosition, 1.0)).xyz;
#endif
#ifdef MASKED
    fragTexCoord = inTexCoord;
#endif

    VsmBasis b = vsmBasis();
    vec3 ls = vsmLightSpace(b, world);
    ivec2 origin = vsmWindowOrigin(b, level);
    vec2 window = ls.xy / vsmPageWorld(level) - vec2(origin);     // 0..32 across the window
    gl_Position = vec4(window / float(VSM_PAGES) * 2.0 - 1.0, 0.5, 1.0);
    fragDepth = vsmDepth(ls.z);
    fragLevelOrigin = ivec3(level, origin);
}
