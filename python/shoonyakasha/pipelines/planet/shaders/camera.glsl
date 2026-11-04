// The camera, terrain and atmosphere uniforms every planet shader reads
// (cameraSet in pipeline.json, bound as set CAMERA_SET).

layout(set = CAMERA_SET, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;     // xyz: unit vector towards the sun
    mat4 invView;
    mat4 invProj;
    vec4 cameraFromCentre; // xyz: the camera's position relative to the planet's centre (m)
    float time;            // seconds since the engine started
    mat4 prevViewProj;     // the frame before's projection times view (the camera turns about its origin)
    vec4 taa;              // xy: this frame's jitter in clip units; z: 1 with temporal anti-aliasing;
                           // w: 1 when the history is to be dropped
    vec4 taaMotion;        // xyz: the camera's position now less the frame before's, from the centre of the
                           // body under it (m)
} camera;

// A clip position moved by this frame's jitter (none without temporal
// anti-aliasing), for the passes whose image the TAA pass resolves.
vec4 jittered(vec4 clip) {
    clip.xy += camera.taa.xy * clip.w;
    return clip;
}

layout(set = CAMERA_SET, binding = 1) uniform TerrainParams {
    vec4 continent;
    vec4 mountains;
    vec4 hills;
    vec4 seed;
    vec4 site;
    vec4 siteShape;
    vec4 detail;
    vec4 climate;
    vec4 biomeView;
    vec4 sites[8];
    vec4 siteShapes[8];
    vec4 lakes[32];
    vec4 lakeShapes[32];
    vec4 riverFrame[4];
    vec4 riverOrigin;
} terrain;

layout(std430, set = CAMERA_SET, binding = 8) readonly buffer RiverData {
    vec4 data[];
} riverData;

layout(set = CAMERA_SET, binding = 2) uniform AtmosphereParams {
    vec4 radii;
    vec4 rayleigh;
    vec4 mie;
    vec4 sun;
    vec4 ozone;
    vec4 settings;
} atmosphere;

// The atmosphere's lookup tables (atmosphere.glsl), built each frame by the
// TransmittanceLut and MultiScatteringLut passes.
layout(set = CAMERA_SET, binding = 3) uniform sampler2D transmittanceLut;
layout(set = CAMERA_SET, binding = 4) uniform sampler2D multiScatteringLut;
#define ATMOSPHERE_TRANSMITTANCE_LUT
#define ATMOSPHERE_MULTI_SCATTERING_LUT

// The sun's shadow cascades (shadows.py, shadow.glsl): the map, one layer
// per cascade, and how to reach it.
layout(set = CAMERA_SET, binding = 5) uniform sampler2DArrayShadow sunShadowMap;
layout(set = CAMERA_SET, binding = 6) uniform ShadowParams {
    mat4 viewProj[4];      // a position relative to the camera to the cascade's clip space (depth 0 at the sun)
    vec4 splits;           // metres along the view where each cascade ends
    vec4 texel;            // metres across one texel of each cascade
    vec4 settings;         // x: 1 with shadows, 0 without; y: the shadow map's size in texels
} shadow;

// The cloud layer (clouds.glsl).
layout(set = CAMERA_SET, binding = 7) uniform CloudParams {
    vec4 layer;
    vec4 shape;
    vec4 wind;
} clouds;
