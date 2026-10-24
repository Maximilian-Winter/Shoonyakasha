#version 450
//
// The ocean's vertex shader: the terrain's chunks again, each vertex moved
// down (or up) along its direction to sea level, the sphere of the planet's
// radius. A skirt vertex keeps its skirt depth below sea level.
//

#define CAMERA_SET 0
#include "camera.glsl"

struct ChunkInstance {
    vec4 origin;           // xyz: the chunk's centre minus the camera's position
    vec4 colour;
};

layout(std430, set = 1, binding = 0) readonly buffer ChunkInstances {
    ChunkInstance instances[];
} chunks;

layout(location = 0) in vec4 inPositionU;
layout(location = 1) in vec4 inNormalV;
layout(location = 2) in vec4 inRadialHeight;

layout(location = 0) out vec3 fragRadial;
layout(location = 1) out float fragWaterDepth;   // metres from sea level down to the sea floor
layout(location = 2) out vec3 fragRelativePos;

void main() {
    ChunkInstance chunk = chunks.instances[gl_InstanceIndex];
    vec3 relativePos = chunk.origin.xyz + inPositionU.xyz - inRadialHeight.xyz * inRadialHeight.w;
    fragRadial = inRadialHeight.xyz;
    fragWaterDepth = -inRadialHeight.w;
    fragRelativePos = relativePos;
    gl_Position = camera.proj * camera.view * vec4(relativePos, 1.0);
}
