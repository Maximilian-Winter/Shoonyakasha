#version 450
//
// The wireframe pass's fragment shader: one colour for every edge.
//

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.02, 0.03, 0.08, 1.0);
}
