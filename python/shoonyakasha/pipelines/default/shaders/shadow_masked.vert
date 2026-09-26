#version 450
// Alpha-tested sun shadow casters (glTF alphaMode MASK): foliage, fences.

#include "common.glsl"

layout(set = 0, binding = 0) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;

layout(push_constant) uniform MaskedShadowDraw {
    mat4 model;
    vec4 baseColorFactor;
    uint cascade;
    float alphaCutoff;
} draw;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = inTexCoord;
    gl_Position = cascades.cascadeViewProj[draw.cascade] * draw.model * vec4(inPosition, 1.0);
}
