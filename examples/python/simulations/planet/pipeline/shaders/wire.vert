#version 450
//
// The wireframe pass's vertex shader: scene.vert's camera-relative
// placement, position only.
//

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = camera.proj * camera.view * (push.model * vec4(inPosition, 1.0));
}
