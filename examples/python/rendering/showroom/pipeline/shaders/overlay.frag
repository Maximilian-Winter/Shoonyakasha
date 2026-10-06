#version 450
// The showroom overlay: samples a panel's texture or a glyph atlas and tints
// it. The sprite_ui_test example's sprite shader.
layout(set = 1, binding = 0) uniform sampler2D spriteTexture;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texel = texture(spriteTexture, fragUV);
    outColor = texel * fragTint;
    if (outColor.a < 0.01) {
        discard;
    }
}
