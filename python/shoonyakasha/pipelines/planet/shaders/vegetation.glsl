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
//     scans      x: 1 on, 0 off; y: their range (m); z, w: the scans' atlas's columns and rows of cells
//     wind       xyz: the wind's velocity (m/s, planet space) TREE_WIND_HEIGHT above the ground under
//                the camera; w: its speed
//     clearings, clearingShapes, clearingAxes   the clearings (clearings.glsl)
//
// Kinds (one indirect draw each, as shoonyakasha.planet.vegetation.KINDS):
// each tree's variant 0 near and far (its impostor) in turn, conifer a and
// b, broadleaf a and b, acacia (0 to 9); then bush, shrub, cactus (10 to
// 12); rock and boulder (13, 14); grass (15); then the scans
// (VEGETATION_SCAN_FIRST on, shoonyakasha.planet.scans.SCANS); then each
// tree's variants 1 to VEGETATION_VARIANTS - 1, near and far each
// (VEGETATION_VARIANT_FIRST on; vegetationVariantKind). Their instances
// follow one another in the kinds' order, a tree's kind holding the
// instances per kind over VEGETATION_VARIANTS, grass's its tufts at most,
// every other kind the instances per kind (vegetationCapacity,
// vegetationFirstInstance). An instance is
// two vec4: (position relative to the camera, height in metres) and (cosine
// and sine of its turn about its up, its tint 0 .. 1 plus an ore rock's
// resource id (resources.glsl) as its whole part, and its colour packed by
// vegetationPackColour, plus VEGETATION_ON_MOON for a rock on the moon and
// VEGETATION_ORE for an ore rock).
// Its up is away from the centre of the body it stands on.
//
// A mesh vertex's normal.w is its part (VEGETATION_PART_*), position.w and
// colour.w its texture's (u, v), colour.rgb its shade. A scan's part is
// VEGETATION_PART_SCAN + 4 x its material's cell in the scans' atlas + 2
// when it is cut out by its alpha + 1 when its bitangent is the tangent's
// cross the normal turned round (vegetationScanPart); its colour.rgb is its
// tangent, and its (u, v) its material's own (vegetationScanUv).

const int VEGETATION_KINDS = 61;
const int VEGETATION_GRASS = 15;
const int VEGETATION_SCAN_FIRST = 16;
const int VEGETATION_SCANS = 15;
const int VEGETATION_ROCK = 13;
const int VEGETATION_BOULDER = 14;
const float VEGETATION_ON_MOON = 4194304.0;   // 2^22, above every packed colour
const float VEGETATION_ORE = 8388608.0;       // 2^23
const int VEGETATION_TREE_KINDS = 10;         // the trees' variant 0 kinds come first, near then far each
const int VEGETATION_VARIANTS = 4;
const int VEGETATION_VARIANT_FIRST = 31;      // the trees' later variants' kinds, after the scans

const int VEGETATION_PART_BARK = 0;
const int VEGETATION_PART_CARD = 1;
const int VEGETATION_PART_GRASS = 2;
const int VEGETATION_PART_IMPOSTOR = 3;
const int VEGETATION_PART_PLAIN = 4;
const int VEGETATION_PART_SCAN = 5;

layout(set = VEGETATION_SET, binding = VEGETATION_PARAMS) uniform VegetationParams {
    vec4 settings;
    vec4 lod;
    vec4 camera;
    vec4 grass;
    vec4 thinning;
    vec4 nearby;
    vec4 rocks;
    vec4 moon;
    vec4 scans;
    vec4 wind;
    vec4 clearings[32];
    vec4 clearingShapes[32];
    vec4 clearingAxes[32];
} vegetation;

// A scan part's atlas cell, whether it is cut out and its bitangent's sign.
void vegetationScanPart(int part, out int cell, out bool cutout, out float sign) {
    int code = part - VEGETATION_PART_SCAN;
    cell = code / 4;
    cutout = (code & 2) != 0;
    sign = (code & 1) != 0 ? -1.0 : 1.0;
}

// A scan cell's rectangle in the scans' atlas: (u0, v0, width, height).
vec4 vegetationScanCell(int cell) {
    vec2 grid = max(vegetation.scans.zw, vec2(1.0));
    vec2 size = 1.0 / grid;
    return vec4(vec2(float(cell % int(grid.x)), float(cell / int(grid.x))) * size, size);
}

// A tree's lean (its up tilted by `lean`, radians along its right (x) and
// its ahead (y), at most VEGETATION_LEAN_MOST) as a whole number added to
// its packed tint: 0 for none, else 1 + one of VEGETATION_LEAN_TURNS
// directions + VEGETATION_LEAN_TURNS x (one of VEGETATION_LEAN_STEPS
// sizes - 1). A tree has no resource, so the tint's whole part is free.
const float VEGETATION_LEAN_MOST = 0.07;
const int VEGETATION_LEAN_TURNS = 32;
const int VEGETATION_LEAN_STEPS = 7;

float vegetationPackLean(vec2 lean) {
    float size = min(length(lean), VEGETATION_LEAN_MOST);
    int step = int(round(size / VEGETATION_LEAN_MOST * float(VEGETATION_LEAN_STEPS)));
    if (step == 0) return 0.0;
    float turn = atan(lean.y, lean.x) / 6.2831853;
    int k = int(round(fract(turn) * float(VEGETATION_LEAN_TURNS))) % VEGETATION_LEAN_TURNS;
    return float(1 + k + VEGETATION_LEAN_TURNS * (step - 1));
}

vec2 vegetationLean(float packed) {
    int code = int(floor(packed));
    if (code <= 0) return vec2(0.0);
    int k = (code - 1) % VEGETATION_LEAN_TURNS, step = (code - 1) / VEGETATION_LEAN_TURNS + 1;
    float turn = 6.2831853 * float(k) / float(VEGETATION_LEAN_TURNS);
    return vec2(cos(turn), sin(turn)) * (VEGETATION_LEAN_MOST * float(step) / float(VEGETATION_LEAN_STEPS));
}

// Whether `kind` is a tree's, near or far, of any variant.
bool vegetationIsTree(int kind) {
    return kind < VEGETATION_TREE_KINDS || kind >= VEGETATION_VARIANT_FIRST;
}

// The kind of `variant` (0 .. VEGETATION_VARIANTS - 1) of the tree whose variant 0's kind is `kind`.
int vegetationVariantKind(int kind, int variant) {
    if (variant == 0) return kind;
    return VEGETATION_VARIANT_FIRST + ((kind / 2) * (VEGETATION_VARIANTS - 1) + variant - 1) * 2 + (kind & 1);
}

// The instances `kind` holds: a tree's `capacity` over VEGETATION_VARIANTS,
// grass's `grassCapacity`, every other kind's `capacity`.
uint vegetationCapacity(int kind, uint capacity, uint grassCapacity) {
    if (kind == VEGETATION_GRASS) return grassCapacity;
    return vegetationIsTree(kind) ? capacity / uint(VEGETATION_VARIANTS) : capacity;
}

// The first instance of `kind`: the kinds before it each holding vegetationCapacity.
uint vegetationFirstInstance(int kind, uint capacity, uint grassCapacity) {
    uint tree = capacity / uint(VEGETATION_VARIANTS);
    uint trees = uint(VEGETATION_TREE_KINDS) * tree;
    if (kind < VEGETATION_TREE_KINDS) return uint(kind) * tree;
    if (kind <= VEGETATION_GRASS) return trees + uint(kind - VEGETATION_TREE_KINDS) * capacity;
    uint scans = trees + uint(VEGETATION_GRASS - VEGETATION_TREE_KINDS) * capacity + grassCapacity;
    if (kind < VEGETATION_VARIANT_FIRST) return scans + uint(kind - VEGETATION_SCAN_FIRST) * capacity;
    return scans + uint(VEGETATION_SCANS) * capacity + uint(kind - VEGETATION_VARIANT_FIRST) * tree;
}

// Whether an instance's packed colour marks it as standing on the moon.
bool vegetationOnMoon(float packed) {
    return mod(floor(packed + 0.5), VEGETATION_ORE) >= VEGETATION_ON_MOON;
}

// Whether an instance's packed colour marks it as an ore rock.
bool vegetationOre(float packed) {
    return packed >= VEGETATION_ORE;
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

// An instance's tint and resource in one float: the resource id as its
// whole part, the tint (held below 1) as its fraction.
float vegetationPackTint(float tint, uint resourceId) {
    return float(resourceId) + min(tint, 0.999);
}

float vegetationTint(float packed) {
    return fract(packed);
}

uint vegetationResource(float packed) {
    return uint(floor(packed));
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

// An impostor's quad corner (shoonyakasha.planet.vegetation's far kinds:
// a vertex's position is the corner of a square about its tree's bounding
// sphere, its colour.rgb the corner's signs (x, y) and the sphere's radius,
// in the tree's units): the quad faces `towards` (from the sphere's centre)
// across the tree's `up`, the tree `height` m tall and `width` wide.
// `offset` is the corner from the plant's base; `local` the corner from the
// sphere's centre in the tree's frame (right, up, ahead), in radii; `view`
// the direction `towards` in the tree's frame.
void vegetationImpostor(vec3 right, vec3 up, vec3 ahead, float height, float width, vec4 position, vec3 corner,
                        vec3 towards, out vec3 offset, out vec3 local, out vec3 view) {
    float radius = corner.z;
    float centreY = position.y - corner.y * radius;
    vec3 axis = normalize(towards);
    vec3 across = cross(up, axis);
    across = dot(across, across) < 1e-8 ? right : normalize(across);
    vec3 above = cross(axis, across);
    vec3 d = (across * corner.x + above * corner.y) * (radius * height * max(width, 1.0));
    offset = up * (centreY * height) + d;
    local = vec3(dot(d, right) / width, dot(d, up), dot(d, ahead) / width) / (radius * height);
    view = vec3(dot(axis, right), dot(axis, up), dot(axis, ahead));
}

