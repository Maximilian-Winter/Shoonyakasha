#version 450
//
// Fixture for ShaderInterfaceValidatorTest: a uniform block holding an array
// of matrices, as sun shadow cascades are published.
//
// CascadeUBO, std140: viewProj 0 (array stride 64, column stride 16), splits 256
//

layout(set = 0, binding = 0) uniform CascadeUBO {
    mat4 viewProj[4];
    vec4 splits;
} cascades;

layout(push_constant) uniform Push {
    uint cascade;
} push;

void main() {
    gl_Position = cascades.viewProj[push.cascade] * cascades.splits;
}
