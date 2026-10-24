// The camera, terrain and atmosphere uniforms every planet shader reads
// (cameraSet in pipeline.json, bound as set CAMERA_SET).

layout(set = CAMERA_SET, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;     // xyz: unit vector towards the sun
    mat4 invView;
    mat4 invProj;
    vec4 cameraFromCentre; // xyz: the camera's position relative to the planet's centre (m)
} camera;

layout(set = CAMERA_SET, binding = 1) uniform TerrainParams {
    vec4 continent;
    vec4 mountains;
    vec4 hills;
    vec4 seed;
    vec4 site;
    vec4 siteShape;
} terrain;

layout(set = CAMERA_SET, binding = 2) uniform AtmosphereParams {
    vec4 radii;
    vec4 rayleigh;
    vec4 mie;
    vec4 sun;
} atmosphere;
