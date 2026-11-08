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
    vec4 gullies;
    vec4 sites[8];
    vec4 siteShapes[8];
    vec4 lakes[32];
    vec4 lakeShapes[32];
    vec4 riverFrame[4];
    vec4 riverOrigin;
    vec4 craters[16];
    vec4 craterShapes[16];
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
    vec4 lightCount;            // x: the local lights in use (lights.glsl)
    vec4 lightPosition[8];      // xyz: relative to the camera (m); w: range (m)
    vec4 lightDirection[8];     // xyz: a spot's unit direction, 0 for a point light; w: its cone's outer cosine
    vec4 lightColour[8];        // rgb: illuminance at 1 m, times the sun's units; w: the cone's inner cosine
} atmosphere;

// The atmosphere's lookup tables (atmosphere.glsl), built each frame by the
// TransmittanceLut, MultiScatteringLut and SkyIrradianceLut passes.
layout(set = CAMERA_SET, binding = 3) uniform sampler2D transmittanceLut;
layout(set = CAMERA_SET, binding = 4) uniform sampler2D multiScatteringLut;
layout(set = CAMERA_SET, binding = 9) uniform sampler2D skyIrradianceLut;
#define ATMOSPHERE_TRANSMITTANCE_LUT
#define ATMOSPHERE_MULTI_SCATTERING_LUT
#define ATMOSPHERE_SKY_IRRADIANCE_LUT

// The sky probe (sky_probe.glsl, the SkyProbe, SkyProbeFilter and SkySh
// passes) and the GGX split-sum table (brdf_lut.comp, from Python).
layout(set = CAMERA_SET, binding = 10) uniform sampler2D skyProbe;
layout(set = CAMERA_SET, binding = 11) uniform sampler2D skySh;
layout(set = CAMERA_SET, binding = 12) uniform sampler2D brdfLut;
#define SKY_PROBE_SAMPLERS

// The nearest site's sky visibility (site_probes.glsl): its frame and grid,
// and its probes.
layout(set = CAMERA_SET, binding = 13) uniform SiteProbeParams {
    vec4 origin;           // xyz: the site's centre relative to the camera (m); w: 1 with a site, 0 without
    vec4 east;             // xyz: the site frame's axes in planet space
    vec4 up;
    vec4 north;
    vec4 gridMin;          // xyz: the grid's least corner in the site's frame (m)
    vec4 spacing;          // xyz: between probes (m)
} site;
layout(set = CAMERA_SET, binding = 14) uniform sampler2D siteVisibility;

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
    vec4 storm;
    vec4 stormShape;
} clouds;
