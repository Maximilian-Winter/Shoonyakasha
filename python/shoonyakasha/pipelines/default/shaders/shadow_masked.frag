#version 450
// Discards the parts of an alpha-tested caster that let light through.

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
#include "material_data.glsl"

layout(push_constant) uniform MaskedShadowDraw {
    mat4 model;
    vec4 baseColorFactor;
    uint cascade;
    float alphaCutoff;
} draw;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec2 fragTexCoord1;

void main() {
    vec2 uv = textureUv(materialData.albedoUv0, materialData.albedoUv1, fragTexCoord, fragTexCoord1);
    if (texture(albedoMap, uv).a * draw.baseColorFactor.a < draw.alphaCutoff) {
        discard;
    }
}
