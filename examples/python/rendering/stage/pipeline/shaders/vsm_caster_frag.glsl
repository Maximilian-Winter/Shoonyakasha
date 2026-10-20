// vsm_caster_frag.glsl - writes a caster's depth into the physical page that
// backs its texel, if that page is drawn this frame. There is no depth
// buffer: the nearest depth wins through an atomic minimum, which orders
// positive floats correctly as unsigned integers. Built as vsm.frag and
// vsm_masked.frag.

#include "common.glsl"

layout(set = 0, binding = 2) uniform Showroom { SHOWROOM_BLOCK } showroom;
layout(set = 1, binding = 0, r32ui) uniform readonly uimage2D vsmPageTable;
layout(set = 1, binding = 2, r32ui) uniform uimage2D vsmPhysical;

#include "vsm.glsl"

#ifdef MASKED
layout(set = 2, binding = 0) uniform sampler2D albedoMap;
layout(push_constant) uniform MaskedShadowDraw {
    mat4 model;
    vec4 baseColorFactor;
    uint cascade;
    float alphaCutoff;
} draw;
layout(location = 2) in vec2 fragTexCoord;
#endif

layout(location = 0) in float fragDepth;
layout(location = 1) flat in ivec3 fragLevelOrigin;

void main() {
    // Slope-scaled bias, as a depth attachment's would be: surfaces steep to
    // the sun are pushed away from it by their depth slope across a texel.
    float slope = abs(dFdx(fragDepth)) + abs(dFdy(fragDepth));
#ifdef MASKED
    float alpha = texture(albedoMap, fragTexCoord).a * draw.baseColorFactor.a;
#endif

    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 page = fragLevelOrigin.yz + texel / VSM_PAGE_SIZE;
    uint entry = imageLoad(vsmPageTable, vsmEntryCoord(fragLevelOrigin.x, page)).r;
    if ((entry & (VSM_VALID | VSM_DIRTY)) != (VSM_VALID | VSM_DIRTY)) return;
#ifdef MASKED
    if (alpha < draw.alphaCutoff) return;
#endif

    float depth = clamp(fragDepth + slope, 0.0, 1.0);
    ivec2 physical = vsmPhysicalPage(entry) * VSM_PAGE_SIZE + (texel & (VSM_PAGE_SIZE - 1));
    imageAtomicMin(vsmPhysical, physical, floatBitsToUint(depth));
}
