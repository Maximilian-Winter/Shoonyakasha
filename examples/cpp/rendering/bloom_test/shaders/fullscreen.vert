#version 450

//
// Fullscreen Triangle Vertex Shader
// 三點成面  一面足矣
// Three points form a plane — one plane suffices
//

layout(location = 0) out vec2 outUV;

void main() {
    // Generate fullscreen triangle without vertex buffer
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
