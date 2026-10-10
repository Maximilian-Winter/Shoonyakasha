#version 450

// Canvas UI quads. Positions are in target pixels, origin top-left, y down.
// Colours arrive sRGB-encoded with straight alpha and leave linear.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uint inMode;

layout(push_constant) uniform Push {
    vec2 targetSize;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;
layout(location = 2) flat out uint outMode;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main() {
    gl_Position = vec4(inPosition / pc.targetSize * 2.0 - 1.0, 0.0, 1.0);
    outUV = inUV;
    outColor = vec4(srgbToLinear(inColor.rgb), inColor.a);
    outMode = inMode & 0xFFu;
}
