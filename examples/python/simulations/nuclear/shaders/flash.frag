#version 450
// A flash, added onto the image: a soft warm glow, brightest at its centre.

layout(location = 0) in vec2 vCorner;
layout(location = 1) in float vBright;

layout(location = 0) out vec4 outColor;

void main() {
    float r2 = dot(vCorner, vCorner);
    if (r2 > 1.0) discard;
    float glow = vBright * (exp(-6.0 * r2) + 0.25 * exp(-2.0 * r2)) * (1.0 - r2);
    outColor = vec4(vec3(1.0, 0.82, 0.55) * glow, 1.0);   // alpha 1: the frame stays opaque
}
