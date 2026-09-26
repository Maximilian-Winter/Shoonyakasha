#version 450
// Skinned sun shadow casters: depth only, posed by the entity's bones.

#include "common.glsl"

layout(set = 0, binding = 0) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;
layout(std430, set = 1, binding = 0) readonly buffer BoneMatrices { mat4 bones[]; };

layout(push_constant) uniform ShadowDraw {
    mat4 model;
    uint cascade;
} draw;

layout(location = 0) in vec3 inPosition;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

void main() {
    mat4 skin = inWeights.x * bones[inJoints.x] + inWeights.y * bones[inJoints.y] +
                inWeights.z * bones[inJoints.z] + inWeights.w * bones[inJoints.w];
    gl_Position = cascades.cascadeViewProj[draw.cascade] * draw.model * skin * vec4(inPosition, 1.0);
}
