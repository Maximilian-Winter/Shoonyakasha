#version 450
//
// Sprite/UI vertex shader
//
// Handles both world-space sprites (projected with the active 3D camera)
// and screen-space UI/text sprites (projected directly from pixel
// coordinates using the current screen resolution), selected per-draw via
// the "screenSpace" push constant.
//

layout(set = 0, binding = 0) uniform SpriteSceneUBO {
    mat4 viewProjection;
    vec2 resolution;
    vec2 padding;
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 tintColor;
    vec4 uvRect;       // x0, y0, x1, y1 in normalized texture space
    float screenSpace;  // 0 = world-space, 1 = screen-space (pixel coords)
    float padding1;
    float padding2;
    float padding3;
} push;

// Shared unit quad: position in [-0.5, 0.5]^2 (z = 0), uv in [0, 1]
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);

    if (push.screenSpace > 0.5) {
        // worldPos.xy is already in pixel coordinates (top-left origin).
        // Map straight to Vulkan NDC - no flip needed, Vulkan clip space
        // is already top-down like screen pixel space.
        vec2 ndc = (worldPos.xy / scene.resolution) * 2.0 - 1.0;
        gl_Position = vec4(ndc, 0.0, 1.0);
    } else {
        gl_Position = scene.viewProjection * worldPos;
    }

    fragUV = mix(push.uvRect.xy, push.uvRect.zw, inUV);
    fragTint = push.tintColor;
}
