// shadow_view.glsl - the matrix a shadow vertex shader renders through.
//
// Sun cascades by default; LOCAL_SPOT or LOCAL_POINT before including this
// file select a spot light's map or a point light's cube face instead. The
// index is the pass's repeat index either way.

#include "common.glsl"

#if defined(LOCAL_SPOT) || defined(LOCAL_POINT)
layout(set = 0, binding = 0) uniform LocalShadowMatrices { DEFAULT_LOCAL_SHADOWS_BLOCK } localShadows;
mat4 shadowViewProj(uint index) {
#ifdef LOCAL_SPOT
    return localShadows.spotViewProj[index];
#else
    return localShadows.pointFaceViewProj[index];
#endif
}
#else
layout(set = 0, binding = 0) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;
mat4 shadowViewProj(uint index) { return cascades.cascadeViewProj[index]; }
#endif
