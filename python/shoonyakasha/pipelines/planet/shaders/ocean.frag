#version 450
//
// The ocean's surface (water.glsl's waterSurface), blended over the sea
// floor already drawn, the water's depth from the terrain function at each
// pixel (terrainSeaFloor).
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "shadow.glsl"
#include "clouds.glsl"
#include "water.glsl"

layout(location = 0) in vec3 fragRadial;
layout(location = 1) in vec3 fragRelativePos;
layout(location = 2) in float fragHorizon;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 radial = normalize(fragRadial);
    outColor = waterSurface(fragRelativePos, radial, fragHorizon, max(-terrainSeaFloor(radial), 0.0));
}
