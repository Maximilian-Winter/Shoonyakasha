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
} camera;

layout(set = CAMERA_SET, binding = 1) uniform TerrainParams {
    vec4 continent;
    vec4 mountains;
    vec4 hills;
    vec4 seed;
    vec4 site;
    vec4 siteShape;
    vec4 detail;
} terrain;

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
