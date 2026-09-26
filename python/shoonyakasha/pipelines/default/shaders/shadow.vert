#version 450
// Opaque sun shadow casters: depth only. One pipeline for every cascade; the
// pass instance picks its matrix through pass.repeatIndex.

#include "common.glsl"

layout(set = 0, binding = 0) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;

layout(push_constant) uniform ShadowDraw {
    mat4 model;
    uint cascade;
} draw;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = cascades.cascadeViewProj[draw.cascade] * draw.model * vec4(inPosition, 1.0);
}
