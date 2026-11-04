#version 450
//
// The rivers' water surface (water.glsl's waterSurface), blended over the
// channel already drawn: the water's depth falls from the depth at the
// centre line to 0 at the edges as the channel's does (rivers.glsl), by
// the square of the place across.
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
layout(location = 2) in float fragAcross;
layout(location = 3) in float fragDepth;

layout(location = 0) out vec4 outColor;

void main() {
    float depth = fragDepth * max(1.0 - fragAcross * fragAcross, 0.0);
    outColor = waterSurface(fragRelativePos, normalize(fragRadial), -1.0, depth);
}
