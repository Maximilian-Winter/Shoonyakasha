#version 450
// A soft round point, added onto what is already there.

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float falloff = exp(-4.0 * dot(d, d));
    outColor = vec4(vColor * falloff, 1.0);
}
