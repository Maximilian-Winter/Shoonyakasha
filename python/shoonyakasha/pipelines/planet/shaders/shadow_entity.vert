#version 450
//
// An entity (a block, the cockpit, a display) into one cascade of the sun's
// shadow map (depth only): its world matrix places it relative to the
// camera, and the cascade's matrix takes it to the map. The pass instance
// gives the cascade (ShadowEntityDraw.cascade, from pass.repeatIndex).
//

layout(set = 0, binding = 0) uniform ShadowParams {
    mat4 viewProj[4];
    vec4 splits;
    vec4 texel;
    vec4 settings;
} shadow;

layout(push_constant) uniform ShadowEntityDraw {
    mat4 model;
    uint cascade;
} draw;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = shadow.viewProj[draw.cascade] * draw.model * vec4(inPosition, 1.0);
}
