#version 450
// G-buffer geometry, skinned meshes.

#include "common.glsl"

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(std430, set = 3, binding = 0) readonly buffer BoneMatrices { mat4 bones[]; };

#include "material_draw.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;
layout(location = 5) in vec4 inTangent;   // xyz along +u, w the bitangent's side; 0 without uvs
layout(location = 7) in vec2 inTexCoord1;     // TEXCOORD_1

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragColor;
layout(location = 4) out vec4 fragClip;          // unjittered, for motion vectors
layout(location = 5) out vec4 fragPrevClip;      // where it was last frame
layout(location = 6) out vec4 fragWorldTangent;
layout(location = 7) out vec2 fragTexCoord1;

void main() {
    mat4 skin = inWeights.x * bones[inJoints.x] + inWeights.y * bones[inJoints.y] +
                inWeights.z * bones[inJoints.z] + inWeights.w * bones[inJoints.w];
    // Last frame's bone matrices follow this frame's (SkeletonComponent::ssboSize).
    uint previous = uint(bones.length()) / 2u;
    mat4 prevSkin = inWeights.x * bones[previous + inJoints.x] + inWeights.y * bones[previous + inJoints.y] +
                    inWeights.z * bones[previous + inJoints.z] + inWeights.w * bones[previous + inJoints.w];
    mat4 model = draw.model * skin;
    vec4 worldPos = model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragWorldNormal = transpose(inverse(mat3(model))) * inNormal;
    // A mirroring model matrix flips the bitangent's side.
    fragWorldTangent = vec4(mat3(model) * inTangent.xyz,
                            determinant(mat3(model)) < 0.0 ? -inTangent.w : inTangent.w);
    fragTexCoord = inTexCoord;
    fragTexCoord1 = inTexCoord1;
    fragColor = vec3(1.0);
    gl_Position = camera.proj * camera.view * worldPos;
    fragClip = gl_Position;
    fragPrevClip = camera.prevViewProj * (draw.prevModel * prevSkin * vec4(inPosition, 1.0));
    gl_Position.xy += taaJitter(camera.frame, camera.taa, camera.resolution) * gl_Position.w;
}
