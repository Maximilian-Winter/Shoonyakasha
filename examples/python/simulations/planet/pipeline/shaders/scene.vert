#version 450
//
// Camera-relative vertex shader. The camera sits at the world origin: the
// view matrix holds only its rotation, and each entity's model matrix places
// it at its position minus the camera's, worked out in double precision on
// the CPU. The projection is reverse-Z with an infinite far plane.
//

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;   // xyz: unit vector towards the sun
} camera;

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
layout(location = 2) out vec2 fragLongLat;

void main() {
    vec4 relativePos = push.model * vec4(inPosition, 1.0);
    fragNormal = normalize(transpose(inverse(mat3(push.model))) * inNormal);
    fragColor = inColor;
    fragLongLat = inTexCoord;
    gl_Position = camera.proj * camera.view * relativePos;
}
