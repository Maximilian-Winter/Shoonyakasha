#version 450
//
// Mandala vertex shader. Projects the shared unit quad with the entity's
// world matrix and hands the fragment shader the quad-local position, which
// mandala.frag scales by the layer size to get world-unit coordinates.
//

layout(set = 0, binding = 0) uniform TempleSceneUBO {
    mat4 viewProjection;
    vec2 resolution;
    float time;
    float padding;
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 tintColor;
    vec4 shape;        // x = layer id, yz = quad size in world units, w = per-entity seed
} push;

// Shared unit quad: position in [-0.5, 0.5]^2 (z = 0), uv in [0, 1]
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragLocal;

void main() {
    gl_Position = scene.viewProjection * push.model * vec4(inPosition, 1.0);
    fragLocal = inPosition.xy;
}
