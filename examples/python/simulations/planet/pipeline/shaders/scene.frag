#version 450
//
// The blocks' fragment shader: the base colour times the vertex colour,
// with face lines where the gridStrength material parameter asks for them
// (shading.glsl), lit through the atmosphere (atmosphere.glsl), in HDR.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "atmosphere.glsl"
#include "shading.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float gridStrength;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragFaceUv;
layout(location = 3) in vec3 fragRelativePos;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 albedo = withGrid(push.baseColorFactor.rgb * fragColor, push.gridStrength, fragFaceUv);
    vec3 p = camera.cameraFromCentre.xyz + fragRelativePos;
    outColor = vec4(lightSurface(albedo, p, normalize(fragNormal), normalize(p)), 1.0);
}
