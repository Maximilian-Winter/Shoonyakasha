#version 450

//
// Bloom Composite Fragment Shader
// 二合為一  明暗相融
// Two become one — light and bloom merge in harmony
//

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D bloomColor;

void main() {
    vec3 scene = texture(sceneColor, inUV).rgb;
    vec3 bloom = texture(bloomColor, inUV).rgb;

    // Additive bloom blend
    vec3 result = scene + bloom;

    // Simple ACES-inspired tone mapping
    result = result / (result + vec3(1.0));

    // Gamma correction
    result = pow(result, vec3(1.0 / 2.2));

    outColor = vec4(result, 1.0);
}
