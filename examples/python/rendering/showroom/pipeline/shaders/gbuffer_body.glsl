// gbuffer_body.glsl - the G-buffer fragment shader. gbuffer_masked.frag builds it with ALPHA_TEST;
// keeping the discard out of gbuffer.frag leaves early depth testing on.
//
//   0 gAlbedo    R8G8B8A8_SRGB       albedo, material occlusion
//   1 gNormal    R16G16_SFLOAT       octahedral world normal
//   2 gMaterial  R8G8B8A8_UNORM      metallic, roughness, clearcoat, clearcoat roughness
//   3 hdrColor   R16G16B16A16_SFLOAT emission; lighting adds onto it
//   4 gVelocity  R16G16_SFLOAT       motion since last frame, in UV: this
//                                    frame's position minus last frame's

#include "common.glsl"
#include "material_draw.glsl"
#include "surface.glsl"
#include "softbox.glsl"

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec2 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmission;
layout(location = 4) out vec2 outVelocity;

layout(location = 4) in vec4 fragClip;
layout(location = 5) in vec4 fragPrevClip;

void main() {
    vec4 baseColor = surfaceBaseColor();
#ifdef ALPHA_TEST
    if (baseColor.a < draw.alphaCutoff) discard;
#endif
    Surface s = evaluateSurface(baseColor);
    // A softbox's diffuser (emissiveFactor.a 2 or more; glTF materials have
    // 1) shows the pattern its light casts, with a grid of a - 2 cells
    // across its short side. The panel is a unit quad scaled to its size.
    if (draw.emissiveFactor.a > 1.5) {
        vec2 halfSize = 0.5 * vec2(length(draw.model[0].xyz), length(draw.model[1].xyz));
        vec2 cells = softboxCells(halfSize, draw.emissiveFactor.a - 2.0);
        s.emissive *= softboxPattern(fragTexCoord, fwidth(fragTexCoord), cells);
    }
    outAlbedo = vec4(s.baseColor.rgb, s.occlusion);
    outNormal = octEncode(s.N);
    outMaterial = vec4(s.metallic, s.roughness, s.clearcoat, s.clearcoatRoughness);
    outEmission = vec4(s.emissive, 0.0);
    outVelocity = fragPrevClip.w > 0.0
        ? (fragClip.xy / fragClip.w - fragPrevClip.xy / fragPrevClip.w) * 0.5
        : vec2(0.0);
}
