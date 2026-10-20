#version 450
// Discards the parts of an alpha-tested caster that let light through.

layout(set = 1, binding = 0) uniform sampler2D albedoMap;

layout(push_constant) uniform MaskedShadowDraw {
    mat4 model;
    vec4 baseColorFactor;
    uint cascade;
    float alphaCutoff;
} draw;

layout(location = 0) in vec2 fragTexCoord;

void main() {
    if (texture(albedoMap, fragTexCoord).a * draw.baseColorFactor.a < draw.alphaCutoff) {
        discard;
    }
}
