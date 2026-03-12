#version 450
//
// Bloom Blur — Vertical Pass
//
// 13-tap Gaussian blur along the vertical axis.
// Completes the separable blur: horizontal followed by vertical
// yields a full 2D Gaussian convolution of scattered light.
//

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(inputTexture, 0));

    // Same 13-tap Gaussian weights as horizontal pass
    const float w[7] = float[](
        0.159577,   // centre
        0.147308,   // offset 1
        0.115877,   // offset 2
        0.077674,   // offset 3
        0.044368,   // offset 4
        0.021595,   // offset 5
        0.008957    // offset 6
    );

    vec3 result = texture(inputTexture, fragTexCoord).rgb * w[0];

    for (int i = 1; i < 7; i++) {
        vec2 offset = vec2(0.0, float(i) * texelSize.y);
        result += texture(inputTexture, fragTexCoord + offset).rgb * w[i];
        result += texture(inputTexture, fragTexCoord - offset).rgb * w[i];
    }

    outColor = vec4(result, 1.0);
}
