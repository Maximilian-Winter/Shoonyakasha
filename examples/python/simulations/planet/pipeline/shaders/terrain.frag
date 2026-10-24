#version 450
//
// The terrain's fragment shader: the chunk's colour, shaded as in
// shading.glsl.
//

#include "shading.glsl"

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;
} camera;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec4 fragColour;
layout(location = 2) in vec2 fragFaceUv;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lit = shade(fragColour.rgb, fragColour.a, fragFaceUv, fragNormal, camera.sunDirection.xyz);
    // The swapchain is sRGB: writing to it encodes the linear colour.
    outColor = vec4(lit, 1.0);
}
