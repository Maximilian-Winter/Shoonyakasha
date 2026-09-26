// common.glsl - shared by the default pipeline's shaders.
//
// Buffer blocks are declared by the shaders themselves, since each puts them
// at a different set; the DEFAULT_*_BLOCK macros below keep the member lists
// in one place so they cannot drift from pipeline.json.

#ifndef DEFAULT_COMMON_GLSL
#define DEFAULT_COMMON_GLSL

// scene.camera.* (pipeline.json "Camera")
#define DEFAULT_CAMERA_BLOCK \
    mat4 view;               \
    mat4 proj;               \
    mat4 invView;            \
    mat4 invProj;            \
    vec4 position;           \
    vec4 params;

// scene.lights[i].* (pipeline.json "Lights")
#define MAX_LIGHTS 16
#define DEFAULT_LIGHTS_BLOCK                      \
    uint lightCount;                              \
    float _lightsPad0, _lightsPad1, _lightsPad2;  \
    vec4 lightsPositionType[MAX_LIGHTS];          \
    vec4 lightsColorIntensity[MAX_LIGHTS];        \
    vec4 lightsDirectionRange[MAX_LIGHTS];        \
    vec4 lightsAttenuation[MAX_LIGHTS];

// scene.shadows.sun.* (pipeline.json "Cascades")
#define DEFAULT_CASCADES_BLOCK \
    mat4 cascadeViewProj[4];   \
    vec4 splits;               \
    vec4 texelWorldSize;       \
    vec4 sunDirection;         \
    uint sunEnabled;           \
    int sunLightIndex;         \
    uint cascadeCount;

// scene.custom.default.* (pipeline.json "Settings"); see README.md
#define DEFAULT_SETTINGS_BLOCK  \
    float exposure;             \
    float iblIntensity;         \
    float skyBlur;              \
    float shadowSoftness;       \
    float shadowNormalBias;     \
    float shadowDepthBias;      \
    float cascadeBlend;         \
    float contactShadowLength;  \
    float contactShadowThickness; \
    float shadowAmbient;        \
    uint debugView;             \
    float aoRadius;             \
    float aoIntensity;          \
    float bloomIntensity;       \
    float bloomRadius;          \
    uint autoExposure;          \
    float exposureAdaptSpeed;   \
    float exposureMinEV;        \
    float exposureMaxEV;        \
    float deltaTime;

// ── Normals ─────────────────────────────────────────────────────
// Octahedral encoding of a unit vector into two values in -1..1.

vec2 octWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 octEncode(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    return n.z >= 0.0 ? n.xy : octWrap(n.xy);
}

vec3 octDecode(vec2 f) {
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

// ── Positions from depth ────────────────────────────────────────
// The camera's projection is OpenGL style (-1..1 depth) and the depth buffer
// stores that NDC depth as is, so a texel's depth goes straight back through
// the inverse projection. The far plane (cleared depth) is 1.

vec3 viewPositionFromDepth(mat4 invProj, vec2 uv, float depth) {
    vec4 p = invProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return p.xyz / p.w;
}

// ── Noise ───────────────────────────────────────────────────────
// Interleaved gradient noise (Jimenez 2014): per-pixel values in 0..1 whose
// neighbours differ as much as possible, so few filter taps look smooth.

float interleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

#endif
