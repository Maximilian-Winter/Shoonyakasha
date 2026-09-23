#version 450
//
// Fixture: compiles only if the test build passes the shared GLSL library's
// include directory to glslc. ShaderInterfaceValidatorTest checks the result.
//

#include "sk/pbr.glsl"
#include "sk/tonemap.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 F0 = baseReflectivity(vec3(0.8, 0.1, 0.1), 0.0);
    vec3 lit = cookTorrance(vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), normalize(vec3(0.3, 0.3, 1.0)),
                            vec3(0.8, 0.1, 0.1), 0.0, 0.5, F0);
    outColor = vec4(ACESFilm(lit), 1.0);
}
