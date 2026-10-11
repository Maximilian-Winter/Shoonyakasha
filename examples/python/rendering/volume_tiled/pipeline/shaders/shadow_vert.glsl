// shadow_vert.glsl - opaque shadow casters, depth only. Built as shadow.vert
// (sun cascades), spot_shadow.vert and point_shadow.vert; the pass instance
// picks its cascade, spot slot or cube face through pass.repeatIndex.

#include "shadow_view.glsl"

layout(push_constant) uniform ShadowDraw {
    mat4 model;
    uint cascade;
} draw;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = shadowViewProj(draw.cascade) * draw.model * vec4(inPosition, 1.0);
}
