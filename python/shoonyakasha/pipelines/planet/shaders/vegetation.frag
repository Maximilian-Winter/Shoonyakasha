#version 450
//
// The vegetation's fragment shader: its colour lit through the atmosphere
// like the blocks (atmosphere.glsl), with the sun's shadow map and the
// clouds' shadows, in HDR. Both sides of each face are drawn; a back face
// of a tree or bush is lit with its normal turned towards the camera, of
// grass (whose normals lean up) with its normal as it is.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "shadow.glsl"
#include "clouds.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColour;
layout(location = 2) in vec3 fragRelativePos;
layout(location = 3) flat in float fragTwoSided;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 p = camera.cameraFromCentre.xyz + fragRelativePos;
    vec3 normal = normalize(gl_FrontFacing || fragTwoSided < 0.5 ? fragNormal : -fragNormal);
    float sunShare = sunShadow(fragRelativePos, normal) * cloudShadow(p);
    outColor = vec4(lightSurface(fragColour, p, normal, normalize(p), sunShare), 1.0);
}
