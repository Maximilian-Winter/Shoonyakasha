#version 450
//
// Both views' vertex shader. Set 0 is the view's camera: CameraUBO for the
// player's view, MapCameraUBO (the camera named "Map") for the map's. The
// two layouts are the same, so one shader serves both passes.
//

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;   // xyz: the camera's world position
    vec4 params;     // near, far, fov (degrees), aspect
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragColor;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragWorldNormal = normalize(transpose(inverse(mat3(push.model))) * inNormal);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    gl_Position = camera.proj * camera.view * worldPos;
}
