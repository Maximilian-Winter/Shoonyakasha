#version 450
//
// Fixture for ShaderInterfaceValidatorTest: one uniform block, one sampler
// and a push constant block, all read so none is optimised away.
//
// CameraUBO, std140: view 0, position 64, count 80, pad 84, lights 96 (stride 16)
// Push, std430:      model 0, tint 64
//

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    vec4 position;
    uint count;
    float pad;
    vec4 lights[4];
} camera;

layout(set = 1, binding = 0) uniform sampler2D albedo;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 tint;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = camera.view * camera.position
             + texture(albedo, vec2(0.5)) * push.tint
             + camera.lights[1] * float(camera.count) * camera.pad
             + push.model[0];
}
