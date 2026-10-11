// vtfs_reduce_body.glsl - the view-space box around all lights, the frame
// the Morton codes are quantised in (van Oosten, Algorithm 6.1), in two
// passes: vtfs_reduce_lights.comp has each of VTFS_REDUCE_GROUPS workgroups
// bound its share of the lights, and vtfs_reduce_bounds.comp, built with
// REDUCE_PARTIALS, bounds those boxes in one workgroup.

#include "vtfs_compute.glsl"

layout(local_size_x = 512) in;

VTFS_LIGHT_BOUNDS(restrict)
#ifndef REDUCE_PARTIALS
VTFS_LIGHTS_VIEW(readonly)
#endif

shared vec3 sharedLo[512];
shared vec3 sharedHi[512];

void main() {
    uint t = gl_LocalInvocationID.x;
    AABB box = emptyAABB();
    vec3 lo = box.lo.xyz, hi = box.hi.xyz;
#ifdef REDUCE_PARTIALS
    for (uint i = t; i < VTFS_REDUCE_GROUPS; i += 512u) {
        lo = min(lo, lightBounds[i].lo.xyz);
        hi = max(hi, lightBounds[i].hi.xyz);
    }
#else
    uint count = lightCount();
    for (uint i = gl_GlobalInvocationID.x; i < count; i += 512u * VTFS_REDUCE_GROUPS) {
        vec4 light = lightsView[i];
        lo = min(lo, light.xyz - light.w);
        hi = max(hi, light.xyz + light.w);
    }
#endif
    sharedLo[t] = lo;
    sharedHi[t] = hi;
    barrier();
    for (uint stride = 256u; stride > 0u; stride >>= 1) {
        if (t < stride) {
            sharedLo[t] = min(sharedLo[t], sharedLo[t + stride]);
            sharedHi[t] = max(sharedHi[t], sharedHi[t + stride]);
        }
        barrier();
    }
    if (t == 0u) {
#ifdef REDUCE_PARTIALS
        lightBounds[VTFS_REDUCE_GROUPS] = AABB(vec4(sharedLo[0], 0.0), vec4(sharedHi[0], 0.0));
#else
        lightBounds[gl_WorkGroupID.x] = AABB(vec4(sharedLo[0], 0.0), vec4(sharedHi[0], 0.0));
#endif
    }
}
