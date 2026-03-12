#version 450
//
// Fullscreen Triangle Vertex Shader
//
// 無為開發 — The easiest way is to not force it
// A single triangle that covers the entire screen
//

// No vertex input needed - generate from gl_VertexIndex

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Generate fullscreen triangle vertices from vertex index
    // Vertex 0: (-1, -1) -> texCoord (0, 0)
    // Vertex 1: ( 3, -1) -> texCoord (2, 0)
    // Vertex 2: (-1,  3) -> texCoord (0, 2)
    // The GPU clips the oversized triangle to the viewport

    vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
    );

    vec2 texCoords[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord = texCoords[gl_VertexIndex];
}
