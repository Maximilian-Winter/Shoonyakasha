// gbuffer_body.glsl - the G-buffer fragment shader. gbuffer_masked.frag builds it with ALPHA_TEST;
// keeping the discard out of gbuffer.frag leaves early depth testing on.
//
//   0 gAlbedo    R8G8B8A8_SRGB       albedo, material occlusion
//   1 gNormal    R16G16_SFLOAT       octahedral world normal
//   2 gMaterial  R8G8_UNORM          metallic, roughness
//   3 hdrColor   R16G16B16A16_SFLOAT emission; lighting adds onto it

#include "common.glsl"
#include "material_draw.glsl"
#include "surface.glsl"

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec2 outNormal;
layout(location = 2) out vec2 outMaterial;
layout(location = 3) out vec4 outEmission;

void main() {
    vec4 baseColor = surfaceBaseColor();
#ifdef ALPHA_TEST
    if (baseColor.a < draw.alphaCutoff) discard;
#endif
    Surface s = evaluateSurface(baseColor);
    outAlbedo = vec4(s.baseColor.rgb, s.occlusion);
    outNormal = octEncode(s.N);
    outMaterial = vec2(s.metallic, s.roughness);
    outEmission = vec4(s.emissive, 0.0);
}
