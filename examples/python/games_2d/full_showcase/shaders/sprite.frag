#version 450
//
// Sprite/UI fragment shader - samples the sprite texture and tints it.
// The same shader serves textured sprites, flat-colored UI panels (a 1x1
// white fallback texture is bound when no texture was set), and baked
// text glyphs (an atlas texture whose alpha channel is glyph coverage).
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
