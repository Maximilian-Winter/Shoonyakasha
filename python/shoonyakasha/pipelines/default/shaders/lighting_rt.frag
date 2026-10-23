#version 460
// glslc: --target-env=vulkan1.2
// Deferred lighting with shadow rays towards spot and point lights and
// traced reflections, for the "raytraced" preset (pass LightingRT); see
// lighting_body.glsl.
#extension GL_EXT_ray_query : require
#define RT_LOCAL_SHADOWS
#define RT_REFLECTIONS
#include "lighting_body.glsl"
