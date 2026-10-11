#version 450
// Geometry of static meshes, for the depth prepass and the forward passes.
// gl_Position is invariant, so both put a vertex at the same depth and the
// forward passes' depth test EQUAL passes.

#include "common.glsl"

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

#include "material_draw.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragColor;
layout(location = 4) out vec4 fragClip;          // unjittered, for motion vectors
layout(location = 5) out vec4 fragPrevClip;      // where it was last frame

invariant gl_Position;

void main() {
    vec4 worldPos = draw.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragWorldNormal = transpose(inverse(mat3(draw.model))) * inNormal;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    gl_Position = camera.proj * camera.view * worldPos;
    fragClip = gl_Position;
    fragPrevClip = camera.prevViewProj * (draw.prevModel * vec4(inPosition, 1.0));
    gl_Position.xy += taaJitter(camera.frame, camera.taa, camera.resolution) * gl_Position.w;
}
