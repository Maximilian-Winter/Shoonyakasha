// The vegetation's parameters and instances (VegetationParams and
// VegetationInstances in pipeline.json), bound as set VEGETATION_SET at
// bindings VEGETATION_PARAMS and VEGETATION_INSTANCES.
//
//     settings   x: 1 on, 0 off; y: range (m); z: cells per cube face side; w: density (times the biome's)
//     lod        x: distance (m) within which the detailed meshes are drawn; y: the bushes' range (m);
//                z: instances per kind; w: the coarsest chunk level that gets vegetation
//     camera     xyz: the camera's position from the planet's centre (m)
//     grass      x: 1 on, 0 off; y: range (m); z: cells per cube face side; w: tufts at most
//     thinning   x: the distance (m) within which every tree kept is drawn; beyond it a tree is
//                drawn with a chance of the square of this distance over its own
//
// Kinds (one indirect draw each, their instances from kind * instances per
// kind): 0 conifer near, 1 conifer far, 2 broadleaf near, 3 broadleaf far,
// 4 bush; and 5 grass, its tufts after the other kinds' instances. An
// instance is two vec4: (position relative to the camera, height in metres)
// and (cosine and sine of its turn about its up, tint 0 .. 1, and its
// foliage's colour packed by vegetationPackColour).

const int VEGETATION_KINDS = 6;
const int VEGETATION_GRASS = 5;

layout(set = VEGETATION_SET, binding = VEGETATION_PARAMS) uniform VegetationParams {
    vec4 settings;
    vec4 lod;
    vec4 camera;
    vec4 grass;
    vec4 thinning;
} vegetation;

// A linear colour (each channel 0 .. 1) in one float: the square roots of
// its channels at 7 bits each, so dark colours keep their steps small,
// summed as whole numbers below 2^21, which a float holds exactly.
float vegetationPackColour(vec3 colour) {
    vec3 q = floor(sqrt(clamp(colour, 0.0, 1.0)) * 127.0 + 0.5);
    return q.x + q.y * 128.0 + q.z * 16384.0;
}

vec3 vegetationUnpackColour(float packed) {
    float v = floor(packed + 0.5);
    vec3 q = vec3(mod(v, 128.0), mod(floor(v / 128.0), 128.0), floor(v / 16384.0));
    vec3 root = q / 127.0;
    return root * root;
}

// The frame of an instance at `relative` (to the camera), whose position
// from the planet's centre is `fromCentre` + `relative`: its up, away from
// the centre, and two horizontal axes turned by its (cos, sin).
void vegetationFrame(vec3 fromCentre, vec3 relative, vec2 turn, out vec3 right, out vec3 up, out vec3 ahead) {
    up = normalize(fromCentre + relative);
    vec3 east = cross(vec3(0.0, 1.0, 0.0), up);
    east = length(east) < 1e-6 ? vec3(1.0, 0.0, 0.0) : normalize(east);
    vec3 north = cross(up, east);
    right = east * turn.x + north * turn.y;
    ahead = cross(right, up);
}
