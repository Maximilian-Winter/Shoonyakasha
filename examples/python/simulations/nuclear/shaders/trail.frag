#version 450
// A free neutron's trail, added onto the image: brightest at the neutron,
// fading to nothing at the tail and toward the ribbon's edges.

layout(location = 0) in float vAlong;
layout(location = 1) in float vAcross;

layout(location = 0) out vec4 outColor;

void main() {
    float fade = pow(1.0 - clamp(vAlong, 0.0, 1.0), 1.5) * (1.0 - vAcross * vAcross);
    outColor = vec4(vec3(0.45, 0.68, 1.0) * fade * 0.8, 1.0);   // alpha 1: the frame stays opaque
}
