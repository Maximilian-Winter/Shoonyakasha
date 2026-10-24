#version 450
//
// The orbit path's vertex shader: points of the path the ship would coast
// along, relative to the camera (from OrbitPoints), drawn as a line strip.
//

#define CAMERA_SET 0
#include "camera.glsl"

layout(location = 0) in vec4 inPosition;   // xyz: the point minus the camera's position

layout(location = 0) out float fragDistance;

void main() {
    fragDistance = length(inPosition.xyz);
    gl_Position = camera.proj * camera.view * vec4(inPosition.xyz, 1.0);
}
