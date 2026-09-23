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

// The engine's full vertex format (VulkanModel.h). Only the position is used;
// the rest are declared so the pipeline's vertex input matches the shader.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

void main() {
    gl_Position = shadow.lightViewProj * push.model * vec4(inPosition, 1.0);
}
