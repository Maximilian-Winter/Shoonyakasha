#version 450

// Canvas UI quads, by mode: 0 the image times the colour, 1 a glyph's signed
// distance field from the atlas, 2 the colour alone. Output is premultiplied.

layout(set = 0, binding = 0) uniform sampler2D glyphAtlas;
layout(set = 0, binding = 1) uniform sampler2D image;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 2) flat in uint inMode;

layout(location = 0) out vec4 outColor;

// Atlas value of a glyph's outline (SdfFont::kOnEdge / 255).
const float kOnEdge = 180.0 / 255.0;

void main() {
    // Both are sampled outside the branches, so their derivatives are defined.
    vec4 texel = texture(image, inUV);
    float distance = texture(glyphAtlas, inUV).r - kOnEdge;
    float coverage = clamp(distance / max(fwidth(distance), 1e-5) + 0.5, 0.0, 1.0);

    vec4 color = inColor;
    if (inMode == 0u) {
        color *= texel;
    } else if (inMode == 1u) {
        color.a *= coverage;
    }
    outColor = vec4(color.rgb * color.a, color.a);
}
