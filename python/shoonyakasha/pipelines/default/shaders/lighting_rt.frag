#version 460
// glslc: --target-env=vulkan1.2
// Deferred lighting with shadow rays towards spot and point lights, traced
// reflections and the irradiance cache's diffuse light, for the "raytraced"
// preset (pass LightingRT); see lighting_body.glsl.
#extension GL_EXT_ray_query : require
#define RT_LOCAL_SHADOWS
#define RT_REFLECTIONS
#define GI
#include "lighting_body.glsl"
