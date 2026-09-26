#version 450
// G-buffer geometry, skinned meshes.

#include "common.glsl"

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(std430, set = 2, binding = 0) readonly buffer BoneMatrices { mat4 bones[]; };

#include "material_draw.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragColor;

void main() {
    mat4 skin = inWeights.x * bones[inJoints.x] + inWeights.y * bones[inJoints.y] +
                inWeights.z * bones[inJoints.z] + inWeights.w * bones[inJoints.w];
    mat4 model = draw.model * skin;
    vec4 worldPos = model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragWorldNormal = transpose(inverse(mat3(model))) * inNormal;
    fragTexCoord = inTexCoord;
    fragColor = vec3(1.0);
    gl_Position = camera.proj * camera.view * worldPos;
}
