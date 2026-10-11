#version 460
// glslc: --target-env=vulkan1.2
// Forward shading of blended materials with ray-traced sun shadows, for the
// "raytraced" preset (pass TransparentRT); see forward_body.glsl.
#extension GL_EXT_ray_query : require
#define RAY_TRACED_SHADOWS
#include "forward_body.glsl"
