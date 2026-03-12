#version 450
//
// Skinned PBR GBuffer Vertex Shader
//
// 骨之舞 — The dance of bones
// Applies skeletal skinning before writing to the GBuffer
//

// Camera UBO (set 0) — matches CameraBufferData layout
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;   // xyz = camera world position, w = padding
    vec4 params;     // x = near, y = far, z = fov (radians), w = aspect
} camera;

// Bone matrices SSBO (set 2) — per-entity skeleton data
// Each entity's SkeletonComponent uploads its bone matrices here
layout(std430, set = 2, binding = 0) readonly buffer BoneMatrices {
    mat4 bones[];
};

// Push constant for model matrix + material params
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float hasNormalMap;
    float hasMetalRoughMap;
    float alphaCutoff;
    float padding1;
} push;

// Vertex inputs (matches 'skinned' vertex format)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in uvec4 inJoints;    // Bone indices
layout(location = 4) in vec4 inWeights;    // Bone weights

// Outputs to fragment shader (same as static pbr_gbuffer.vert)
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragColor;

void main() {
    // Compute skin matrix from 4 bone influences
    mat4 skinMatrix =
        inWeights.x * bones[inJoints.x] +
        inWeights.y * bones[inJoints.y] +
        inWeights.z * bones[inJoints.z] +
        inWeights.w * bones[inJoints.w];

    // Apply skinning to position and normal
    vec4 skinnedPos = skinMatrix * vec4(inPosition, 1.0);
    vec3 skinnedNormal = normalize(mat3(skinMatrix) * inNormal);

    // Transform to world space
    vec4 worldPos = push.model * skinnedPos;
    fragWorldPos = worldPos.xyz;

    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragWorldNormal = normalize(normalMatrix * skinnedNormal);

    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    fragColor = vec3(1.0);  // Skinned meshes don't use vertex color

    // Final clip-space position
    gl_Position = camera.proj * camera.view * worldPos;
}
