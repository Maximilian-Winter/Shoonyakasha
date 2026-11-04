#version 450
//
// The glowing quads' vertex shader (the Glow pass, glow.frag),
// camera-relative as scene.vert is. The pass draws after the TAA resolve,
// so its positions are not jittered.
//

#define CAMERA_SET 0
#include "camera.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragRelativePos;

void main() {
    vec4 relativePos = push.model * vec4(inPosition, 1.0);
    fragNormal = normalize(mat3(push.model) * inNormal);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragRelativePos = relativePos.xyz;
    gl_Position = camera.proj * camera.view * relativePos;
}
