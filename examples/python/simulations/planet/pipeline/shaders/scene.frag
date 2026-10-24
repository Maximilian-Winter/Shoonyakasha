#version 450
//
// The blocks' fragment shader: the base colour times the vertex colour,
// shaded as in shading.glsl, with face lines where the gridStrength material
// parameter asks for them.
//

#include "shading.glsl"

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float gridStrength;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragFaceUv;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lit = shade(push.baseColorFactor.rgb * fragColor, push.gridStrength, fragFaceUv, fragNormal,
                     camera.sunDirection.xyz);
    // The swapchain is sRGB: writing to it encodes the linear colour.
    outColor = vec4(lit, 1.0);
}
