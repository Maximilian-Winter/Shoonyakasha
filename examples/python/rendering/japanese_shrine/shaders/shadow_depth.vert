#version 450
//
// Shadow map vertex shader: projects geometry into the sun's view. Only depth
// is written, so there is no fragment work beyond the empty shadow_depth.frag.
//

// Sun shadow data (set 0) — dot-path filled from scene.custom.shadow.*
layout(set = 0, binding = 0) uniform ShadowUBO {
    mat4 lightViewProj;  // world -> sun clip space, Vulkan depth range [0, 1]
    vec4 params;         // read by shrine_lighting.frag
} shadow;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

// Position only; the engine's other vertex attributes are left unread.
layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = shadow.lightViewProj * push.model * vec4(inPosition, 1.0);
}
