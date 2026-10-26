#version 450
//
// The entities' fragment shader (the blocks, the ship's model): the base
// colour times the vertex colour times the material's albedo texture (white
// where it has none), with face lines where the gridStrength material
// parameter asks for them (shading.glsl), lit through the atmosphere
// (atmosphere.glsl), in HDR. Both sides of each face are drawn, a back face
// lit with its normal turned towards the camera.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "shading.glsl"
#include "shadow.glsl"
#include "clouds.glsl"

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

layout(set = 1, binding = 0) uniform sampler2D albedoMap;

void main() {
    vec3 base = push.baseColorFactor.rgb * fragColor * texture(albedoMap, fragFaceUv).rgb;
    vec3 albedo = withGrid(base, push.gridStrength, fragFaceUv);
    vec3 p = camera.cameraFromCentre.xyz + fragRelativePos;
    vec3 normal = normalize(gl_FrontFacing ? fragNormal : -fragNormal);
    float sunShare = sunShadow(fragRelativePos, normal) * cloudShadow(p);
    outColor = vec4(lightSurface(albedo, p, normal, normalize(p), sunShare), 1.0);
}
