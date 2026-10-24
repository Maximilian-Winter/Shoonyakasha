#version 450
//
// World canvases (the cockpit's displays): their texture, in the material's
// emissiveMap, as light of emissiveFactor's strength, in HDR.
//

layout(set = 1, binding = 1) uniform sampler2D emissiveMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float gridStrength;
    float padding0;
    float padding1;
    float padding2;
    vec4 emissiveFactor;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragRelativePos;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(texture(emissiveMap, fragTexCoord).rgb * push.emissiveFactor.rgb, 1.0);
}
