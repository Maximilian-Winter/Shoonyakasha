#version 460
// glslc: --target-env=vulkan1.2
// Deferred lighting with shadow rays towards each shadowed spot and point
// light, for the "raytraced" preset (pass LightingRT); see lighting_body.glsl.
#extension GL_EXT_ray_query : require
#define RT_LOCAL_SHADOWS
#include "lighting_body.glsl"
