#version 450
//
// The vegetation into one cascade of the sun's shadow map, the cascade
// from the pass's repeat index: each plant placed, turned and scaled as
// vegetation.vert places it (leaning as it leans), without the sway, an impostor's quad facing
// the sun (the cascade's depth axis, turned to the sky) instead of the
// camera, with its point and the sun's direction in the tree's frame. Plants farther
// from the camera than the cascade's far split and SHADOW_VEGETATION_MARGIN
// are left out of it: their vertices go to one point outside the map. The
// fragment shader (shadow_vegetation.frag) gets the part and the texture's
// (u, v), a scan's cell in the scans' atlas, and an impostor's point and
// the sun's direction in its tree's frame.
//

layout(set = 0, binding = 0) uniform ShadowParams {
    mat4 viewProj[4];
    vec4 splits;
    vec4 texel;
    vec4 settings;
} shadow;

#define VEGETATION_SET 1
#define VEGETATION_PARAMS 0
#include "vegetation.glsl"

layout(std430, set = 1, binding = 1) readonly buffer VegetationInstances {
    vec4 data[];
} instances;

layout(push_constant) uniform Cascade {
    uint index;
} cascade;

layout(location = 0) in vec4 inPosition;   // w: u
layout(location = 1) in vec4 inNormal;     // w: the part
layout(location = 2) in vec4 inColour;     // w: v

layout(location = 0) flat out int fragPart;
layout(location = 1) out vec2 fragUv;
layout(location = 2) flat out vec4 fragCell;
layout(location = 3) out vec3 fragLocal;
layout(location = 4) flat out vec3 fragView;

const float SHADOW_VEGETATION_MARGIN = 100.0;

void main() {
    vec4 placed = instances.data[gl_InstanceIndex * 2];
    vec4 look = instances.data[gl_InstanceIndex * 2 + 1];
    mat4 viewProj = shadow.viewProj[cascade.index];
    fragPart = int(inNormal.w + 0.5);
    fragUv = vec2(inPosition.w, inColour.w);
    fragCell = vec4(0.0);
    if (fragPart >= VEGETATION_PART_SCAN) {
        int cell;
        bool cutout;
        float sign;
        vegetationScanPart(fragPart, cell, cutout, sign);
        fragCell = vegetationScanCell(cell);
    }
    if (length(placed.xyz) > shadow.splits[cascade.index] + SHADOW_VEGETATION_MARGIN) {
        gl_Position = vec4(2.0, 2.0, 0.5, 1.0);
        return;
    }
    float width = vegetationWidth(vegetationTint(look.z));
    vec3 right, up, ahead;
    vegetationFrame(vegetationCameraFromCentre(look.w), placed.xyz, look.xy, right, up, ahead);
    vec3 offset;
    fragLocal = vec3(0.0);
    fragView = vec3(0.0, 1.0, 0.0);
    if (fragPart == VEGETATION_PART_IMPOSTOR) {
        vec3 depthAxis = vec3(viewProj[0][2], viewProj[1][2], viewProj[2][2]);
        vec3 sun = dot(depthAxis, up) < 0.0 ? -depthAxis : depthAxis;
        vegetationImpostor(right, up, ahead, placed.w, width, inPosition, inColour.xyz, sun, offset, fragLocal,
                           fragView);
    } else {
        offset = (right * inPosition.x * width + up * inPosition.y + ahead * inPosition.z * width) * placed.w;
    }
    if (fragPart == VEGETATION_PART_BARK || fragPart == VEGETATION_PART_CARD || fragPart == VEGETATION_PART_IMPOSTOR) {
        vec2 lean = vegetationLean(look.z);
        offset += (right * lean.x + ahead * lean.y) * (inPosition.y * placed.w);
    }
    gl_Position = viewProj * vec4(placed.xyz + offset, 1.0);
}
