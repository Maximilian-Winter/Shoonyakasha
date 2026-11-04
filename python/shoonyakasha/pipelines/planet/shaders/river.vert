#version 450
//
// The rivers' water (shoonyakasha.planet.rivers.ribbon): each vertex's
// position relative to the height grid's centre point, moved to the
// camera's by terrain.riverOrigin (that point less the camera's position),
// and lifted along its up by LIFT_PER_METRE of its distance from the camera,
// so coarser terrain far off does not hide it. Its up, its place across the
// river (-1 at one edge, 1 at the other) and the depth at the centre line
// are handed on. Positions are jittered for the TAA pass (camera.glsl's
// jittered).
//

#define CAMERA_SET 0
#include "camera.glsl"

layout(location = 0) in vec4 inPosition;   // xyz: relative to the grid's centre point (m); w: across
layout(location = 1) in vec4 inUpDepth;    // xyz: unit up; w: depth at the centre line (m)

layout(location = 0) out vec3 fragRadial;
layout(location = 1) out vec3 fragRelativePos;
layout(location = 2) out float fragAcross;
layout(location = 3) out float fragDepth;

const float LIFT_PER_METRE = 0.0005;

void main() {
    vec3 relativePos = inPosition.xyz + terrain.riverOrigin.xyz;
    relativePos += inUpDepth.xyz * (LIFT_PER_METRE * length(relativePos));
    fragRadial = inUpDepth.xyz;
    fragRelativePos = relativePos;
    fragAcross = inPosition.w;
    fragDepth = inUpDepth.w;
    gl_Position = jittered(camera.proj * camera.view * vec4(relativePos, 1.0));
}
