#version 450
//
// The lakes' surface (water.glsl's waterSurface), blended over the ground
// already drawn: a pixel is drawn where the water of the lake its chunk
// took (lake.vert) covers it (lakes.glsl's lakeCovers), with that water's
// depth there.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "shadow.glsl"
#include "clouds.glsl"
#include "lakes.glsl"
#include "water.glsl"

layout(location = 0) in vec3 fragRadial;
layout(location = 1) in vec3 fragRelativePos;
layout(location = 2) in float fragHorizon;
layout(location = 3) flat in int fragLake;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 radial = normalize(fragRadial);
    float depth;
    if (!lakeCovers(fragLake, radial, depth)) discard;
    outColor = waterSurface(fragRelativePos, radial, fragHorizon, depth);
}
