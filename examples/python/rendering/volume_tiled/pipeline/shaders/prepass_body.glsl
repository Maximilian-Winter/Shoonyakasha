// prepass_body.glsl - the depth prepass of Volume Tiled Forward Shading.
// Besides depth it writes what the screen-space passes need, so they run
// as in the default pipeline: the normal (sun shadow mask, ambient
// occlusion) and the motion (TAA). prepass_masked.frag builds it with
// ALPHA_TEST; keeping the discard out of prepass.frag leaves early depth
// testing on.
//
//   0 gNormal    R16G16_SFLOAT  octahedral world normal, normal map applied
//   1 gVelocity  R16G16_SFLOAT  motion since last frame, in UV: this
//                               frame's position minus last frame's

#include "common.glsl"
#include "material_draw.glsl"
#define EXPOSURE_SET 2
#include "surface.glsl"

layout(location = 0) out vec2 outNormal;
layout(location = 1) out vec2 outVelocity;

layout(location = 4) in vec4 fragClip;
layout(location = 5) in vec4 fragPrevClip;

void main() {
    vec4 baseColor = surfaceBaseColor();
#ifdef ALPHA_TEST
    if (baseColor.a < draw.alphaCutoff) discard;
#endif
    Surface s = evaluateSurface(baseColor);
    outNormal = octEncode(s.N);
    outVelocity = fragPrevClip.w > 0.0
        ? (fragClip.xy / fragClip.w - fragPrevClip.xy / fragPrevClip.w) * 0.5
        : vec2(0.0);
}
