#version 450
// Copies an image texel for texel: TAA's result into its persistent history.

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texelFetch(source, ivec2(gl_FragCoord.xy), 0);
}
