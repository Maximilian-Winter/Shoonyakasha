#version 450
//
// The vegetation into one cascade of the sun's shadow map (depth only), the
// cascade from the pass's repeat index. Plants farther from the camera than
// the cascade's far split and SHADOW_VEGETATION_MARGIN are left out of it:
// their vertices go to one point outside the map.
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

layout(location = 0) in vec4 inPosition;

const float SHADOW_VEGETATION_MARGIN = 100.0;

void main() {
    vec4 placed = instances.data[gl_InstanceIndex * 2];
    vec4 look = instances.data[gl_InstanceIndex * 2 + 1];
    if (length(placed.xyz) > shadow.splits[cascade.index] + SHADOW_VEGETATION_MARGIN) {
        gl_Position = vec4(2.0, 2.0, 0.5, 1.0);
        return;
    }
    vec3 right, up, ahead;
    vegetationFrame(vegetation.camera.xyz, placed.xyz, look.xy, right, up, ahead);
    vec3 relativePos = placed.xyz + (right * inPosition.x + up * inPosition.y + ahead * inPosition.z) * placed.w;
    gl_Position = shadow.viewProj[cascade.index] * vec4(relativePos, 1.0);
}
