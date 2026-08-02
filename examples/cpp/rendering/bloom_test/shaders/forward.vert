#version 450

//
// Forward Pass Vertex Shader - Fullscreen Triangle
// 無頂點而成三角  三角而覆全屏
// No vertices yet a triangle forms — the triangle covers all
//

layout(location = 0) out vec2 outUV;

void main() {
    // Generate fullscreen triangle from vertex ID
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
