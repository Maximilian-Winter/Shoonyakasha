// common.glsl - the camera and the settings (set 1, "settingsSet"), and
// helpers shared by the ReSTIR passes.

#ifndef COMMON_GLSL
#define COMMON_GLSL

layout(set = 1, binding = 0) uniform Camera {
    mat4 view;
    mat4 invView;
    mat4 invProj;
    mat4 viewProj;
    mat4 prevViewProj;
    vec4 position;
    vec2 resolution;     // full resolution, pixels
    uint frame;
} camera;

layout(set = 1, binding = 1) uniform Settings {
    uint view;           // 0 image, 1 indirect diffuse, 2 irradiance cache, 3 direct light, 4 reflections
    uint toneMapper;
    uint freeze;         // 1: the irradiance cache neither moves nor traces
    uint stages;         // the STAGE_* that run
    float exposure;
    float environmentIntensity;
    float sunAngle;      // angular radius of directional lights, degrees
    float pad;
} settings;

const uint VIEW_INDIRECT = 1u;
const uint VIEW_CACHE = 2u;
const uint VIEW_DIRECT = 3u;
const uint VIEW_REFLECTIONS = 4u;

const uint STAGE_TEMPORAL = 1u;          // diffuse: temporal reservoir resampling
const uint STAGE_SPATIAL = 2u;           // diffuse: the two spatial resampling passes
const uint STAGE_NEAR_FIELD = 4u;        // diffuse: near hits from the raw candidates in the resolve
const uint STAGE_TEMPORAL_FILTER = 8u;   // diffuse
const uint STAGE_SPATIAL_FILTER = 16u;   // diffuse
const uint STAGE_RTR_RESTIR = 32u;       // reflections: temporal reservoir resampling and validation
const uint STAGE_RTR_FILTERS = 64u;      // reflections: the temporal filter and the spatial cleanup

const float SKY_DIST = 1e4;
const float GOLDEN_ANGLE = 2.39996323;
const float TAU = 6.28318530718;

// Every third frame the reservoirs' samples are traced again (validated),
// instead of new candidates being traced to full length.
bool isValidationFrame() { return camera.frame % 3u == 0u; }
bool isTracingFrame() { return !isValidationFrame(); }

// The pixel of each 2 x 2 block of the full resolution that a half
// resolution pixel stands for this frame; it turns every frame.
ivec2 halfresOffset() {
    const ivec2 offsets[4] = ivec2[4](ivec2(1, 1), ivec2(1, 0), ivec2(0, 0), ivec2(0, 1));
    return offsets[camera.frame & 3u];
}

ivec2 hiPixel(ivec2 px) { return px * 2 + halfresOffset(); }

vec2 pixelUv(ivec2 px) { return (vec2(px) + 0.5) / camera.resolution; }

// How much two view depths differ, relative to the first.
float depthRelativeDiff(float primary, float secondary) {
    return abs(max(1e-20, secondary) / max(1e-20, primary) - 1.0);
}

vec3 directionWorldToView(vec3 v) { return mat3(camera.view) * v; }

// A point's uv and view depth on screen this frame.
vec3 worldToUvDepth(vec3 p) {
    vec4 clip = camera.viewProj * vec4(p, 1.0);
    return vec3(clip.xy / clip.w * 0.5 + 0.5, clip.w);
}

vec3 viewDirectionAt(vec2 uv) {
    vec4 target = camera.invProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    return normalize((camera.invView * vec4(normalize(target.xyz / target.w), 0.0)).xyz);
}

// Where a secondary ray from a G-buffer surface starts: off the surface
// along its normal and back towards the camera, by an amount that grows
// with the point's distance.
vec3 secondaryRayOrigin(vec3 position, vec3 normal, float depth) {
    vec3 dir = normalize(position - camera.position.xyz);
    vec3 a = abs(position);
    float scale = max(max(a.x, a.y), max(a.z, depth));
    return position + (normal - dir) * max(1e-3, scale * 1e-5);
}

// Where the camera was last frame: the point last frame's view-projection
// takes to x = y = w = 0.
vec3 previousEyePosition() {
    mat4 m = camera.prevViewProj;
    vec4 rx = vec4(m[0].x, m[1].x, m[2].x, m[3].x);
    vec4 ry = vec4(m[0].y, m[1].y, m[2].y, m[3].y);
    vec4 rw = vec4(m[0].w, m[1].w, m[2].w, m[3].w);
    mat3 a = transpose(mat3(rx.xyz, ry.xyz, rw.xyz));
    return inverse(a) * -vec3(rx.w, ry.w, rw.w);
}

// 1 - cos theta, kept in half floats, where it is more precise near 1.
float encodeCosTheta(float x) { return 1.0 - x; }
float decodeCosTheta(float x) { return 1.0 - x; }

// Packing of the reservoirs' payload: the half resolution pixel that holds
// the sample's ray.
uint pixelPayload(ivec2 px) { return uint(px.x) | (uint(px.y) << 16); }
ivec2 payloadPixel(uint payload) { return ivec2(payload & 0xFFFFu, payload >> 16); }

float interleavedGradientNoise(vec2 px) {
    return fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y));
}

uint hashPixel(ivec2 px, uint salt) {
    uint h = uint(px.x) * 73856093u ^ uint(px.y) * 19349663u ^ salt * 83492791u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}

#endif
