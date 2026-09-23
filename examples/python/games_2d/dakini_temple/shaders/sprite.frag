#version 450
//
// Sprite/UI fragment shader - samples the sprite texture and tints it.
// Baked text glyphs use an atlas texture whose alpha channel is glyph coverage.
//

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
