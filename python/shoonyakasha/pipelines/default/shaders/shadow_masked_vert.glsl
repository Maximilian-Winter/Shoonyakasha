// shadow_masked_vert.glsl - alpha-tested shadow casters (glTF alphaMode MASK). Built as
// shadow_masked.vert, spot_shadow_masked.vert and point_shadow_masked.vert.
// Drawn instanced: each instance's model matrix comes from the per-instance
// vertex buffer (location 8).

#include "shadow_view.glsl"

layout(push_constant) uniform MaskedShadowDraw {
    mat4 model;
    vec4 baseColorFactor;
    uint cascade;
    float alphaCutoff;
} draw;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;
layout(location = 8) in mat4 instanceModel;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = inTexCoord;
    gl_Position = shadowViewProj(draw.cascade) * instanceModel * vec4(inPosition, 1.0);
}
