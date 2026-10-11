#version 450
// Forward shading of alpha-tested materials (glTF alphaMode MASK); see forward_body.glsl.
#define OPAQUE
#define ALPHA_TEST
#include "forward_body.glsl"
