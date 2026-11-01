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
//     nearby     x: radius (m) of the plants listed in NearbyTrees; y: stamp; z: capacity
//     rocks      x: 1 on, 0 off; y: the small rocks' range (m); z: their grid's cells per cube
//                face side; w: the entries in TakenRocks
//     moon       xyz: the moon's centre less the camera's position (m)
//
// Kinds (one indirect draw each, their instances from kind * instances per
// kind, as shoonyakasha.planet.vegetation.KINDS): each tree near and far
// (its impostor) in turn, conifer a and b, broadleaf a and b, acacia (0 to
// 9); then bush, shrub, cactus (10 to 12); rock and boulder (13, 14); and
// grass (15), its tufts after the other kinds' instances. An instance is
// two vec4: (position relative to the camera, height in metres) and (cosine
// and sine of its turn about its up, tint 0 .. 1, and its colour packed by
// vegetationPackColour, plus VEGETATION_ON_MOON for a rock on the moon).
// Its up is away from the centre of the body it stands on.
//
// A mesh vertex's normal.w is its part (VEGETATION_PART_*), position.w and
// colour.w its texture's (u, v), colour.rgb its shade.

const int VEGETATION_KINDS = 16;
const int VEGETATION_GRASS = 15;
const int VEGETATION_ROCK = 13;
const int VEGETATION_BOULDER = 14;
const float VEGETATION_ON_MOON = 4194304.0;   // 2^22, above every packed colour
const int VEGETATION_TREE_KINDS = 10;         // the trees' kinds come first, near then far each

const int VEGETATION_PART_BARK = 0;
const int VEGETATION_PART_CARD = 1;
const int VEGETATION_PART_GRASS = 2;
const int VEGETATION_PART_IMPOSTOR = 3;
const int VEGETATION_PART_PLAIN = 4;

layout(set = VEGETATION_SET, binding = VEGETATION_PARAMS) uniform VegetationParams {
    vec4 settings;
    vec4 lod;
    vec4 camera;
    vec4 grass;
    vec4 thinning;
    vec4 nearby;
    vec4 rocks;
    vec4 moon;
} vegetation;

// Whether an instance's packed colour marks it as standing on the moon.
bool vegetationOnMoon(float packed) {
    return packed >= VEGETATION_ON_MOON;
}

// The camera's position from the centre of the body an instance stands on.
vec3 vegetationCameraFromCentre(float packed) {
    return vegetationOnMoon(packed) ? -vegetation.moon.xyz : vegetation.camera.xyz;
}

// A linear colour (each channel 0 .. 1) in one float: the square roots of
// its channels at 7 bits each, so dark colours keep their steps small,
// summed as whole numbers below 2^21, which a float holds exactly.
float vegetationPackColour(vec3 colour) {
    vec3 q = floor(sqrt(clamp(colour, 0.0, 1.0)) * 127.0 + 0.5);
    return q.x + q.y * 128.0 + q.z * 16384.0;
}

vec3 vegetationUnpackColour(float packed) {
    float v = mod(floor(packed + 0.5), VEGETATION_ON_MOON);
    vec3 q = vec3(mod(v, 128.0), mod(floor(v / 128.0), 128.0), floor(v / 16384.0));
    vec3 root = q / 127.0;
    return root * root;
}

// How much wider than its mesh a plant is drawn, from its tint (0 .. 1):
// 0.85 to 1.15, unrelated to the tint's own effect.
float vegetationWidth(float tint) {
    return mix(0.85, 1.15, fract(tint * 7.31));
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

// An impostor's frame: its quad stands along `up` and faces `towards`
// (flattened square to `up`); `right` is across it, `facing` out of it.
void vegetationBillboard(vec3 up, vec3 towards, out vec3 right, out vec3 facing) {
    facing = towards - up * dot(towards, up);
    facing = length(facing) < 1e-6 ? cross(up, vec3(1.0, 0.0, 0.0)) : normalize(facing);
    right = normalize(cross(up, facing));
}
