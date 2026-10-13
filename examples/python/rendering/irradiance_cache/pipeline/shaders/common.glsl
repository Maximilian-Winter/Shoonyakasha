// common.glsl - the camera and the settings, at set 1 ("settingsSet").

#ifndef COMMON_GLSL
#define COMMON_GLSL

layout(set = 1, binding = 0) uniform Camera {
    mat4 invView;
    mat4 invProj;
    vec4 position;
    vec2 resolution;
    uint frame;
} camera;

layout(set = 1, binding = 1) uniform Settings {
    uint reset;          // 1: start the image's average over
    uint view;           // 0 image, 1 cache, 2 entries, 3 direct light only
    uint toneMapper;
    uint freeze;         // 1: the cache neither moves nor traces
    float exposure;
    float environmentIntensity;
    float sunAngle;      // angular radius of directional lights, degrees
    float history;       // weight of a new frame in the image's average, at least
} settings;

const uint VIEW_CACHE = 1u;
const uint VIEW_ENTRIES = 2u;
const uint VIEW_DIRECT = 3u;

#endif
