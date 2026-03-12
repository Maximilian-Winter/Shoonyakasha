#version 450
//
// PBR GBuffer Vertex Shader
//
// 位先於動 — Position before action
// Prepares geometry data for the GBuffer pass
//

// Camera UBO (set 0) — matches CameraBufferData from StandardBufferManager
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;   // xyz = camera world position, w = padding
    vec4 params;     // x = near, y = far, z = fov (radians), w = aspect
} camera;

// Push constant for model matrix
layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

// Vertex inputs (matches engine's Vertex struct in VulkanModel.h)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragColor;

void main() {
    // Transform to world space
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragWorldNormal = normalize(normalMatrix * inNormal);

    // Pass through texture coordinates and vertex color
    fragTexCoord = inTexCoord;
    fragColor = inColor;

    // Final clip-space position
    gl_Position = camera.proj * camera.view * worldPos;
}
