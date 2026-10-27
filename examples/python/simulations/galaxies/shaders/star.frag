#version 450
// A soft round point: the vertex colour under a Gaussian across the point.

layout(location = 0) in vec3 color;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;
    outColor = vec4(color * exp(-3.0 * r2), 1.0);
}
