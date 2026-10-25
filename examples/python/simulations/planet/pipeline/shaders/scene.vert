#version 450
//
// The entities' vertex shader (the blocks, the ship's model), camera-relative. The camera sits at the world
// origin: the view matrix holds only its rotation, and each entity's model
// matrix places it at its position minus the camera's, worked out in double
// precision on the CPU. The projection is reverse-Z with an infinite far
// plane.
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
layout(location = 2) out vec2 fragFaceUv;
layout(location = 3) out vec3 fragRelativePos;

void main() {
    vec4 relativePos = push.model * vec4(inPosition, 1.0);
    fragNormal = normalize(transpose(inverse(mat3(push.model))) * inNormal);
    fragColor = inColor;
    fragFaceUv = inTexCoord;
    fragRelativePos = relativePos.xyz;
    gl_Position = camera.proj * camera.view * relativePos;
}
