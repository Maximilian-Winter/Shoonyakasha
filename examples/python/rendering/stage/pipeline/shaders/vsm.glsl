// vsm.glsl - the sun's virtual shadow map: a clipmap of VSM_LEVELS levels
// around the camera, each 4096 x 4096 virtual texels in 32 x 32 pages of
// 128 x 128. Only the pages that visible pixels need are backed by memory,
// pages of an 8192 x 8192 pool of 32-bit depth (4096 pages), and a page
// keeps its depth from frame to frame until something invalidates it.
//
// Level k covers showroom.vsmFirstLevel * 2^k world units across, centred on
// the camera and snapped to whole pages. Pages are addressed by their
// absolute position in light space, wrapped into the level's 32 x 32 table
// (toroidally), so when the camera moves only the pages that newly come into
// view need rendering: the rest keep their place in the table and the pool.
//
// Images (vsmSet, all R32_UINT storage images):
//   vsmPageTable  32 x (32 * levels)  per virtual page: physical page (bits 0-11),
//                                     VSM_VALID, VSM_DIRTY (render this frame)
//   vsmPageTag    32 x (32 * levels)  the absolute page each table entry holds
//   vsmPhysical   8192 x 8192         depth, as float bits; 0 nearest the sun
//   vsmOwner      64 x 64             per physical page: table entry + 1, 0 when free
//   vsmAge        64 x 64             per physical page: frame it was last needed
//   vsmRequest    32 x (32 * levels)  pages visible pixels asked for this frame
//   vsmState      16 x 1              frame counter and what invalidates every page
//   vsmLevelInfo  levels x 1          pages to render in each level this frame
//
// The including shader declares the Showroom block as `showroom`
// (SHOWROOM_BLOCK) before including this file.

#ifndef SHOWROOM_VSM_GLSL
#define SHOWROOM_VSM_GLSL

#define VSM_LEVELS 8
#define VSM_PAGES 32                 // pages across a level
#define VSM_PAGE_SIZE 128            // texels across a page
#define VSM_LEVEL_SIZE 4096          // texels across a level
#define VSM_POOL_PAGES 64            // physical pages across the pool
#define VSM_ENTRIES (VSM_LEVELS * VSM_PAGES * VSM_PAGES)
#define VSM_POOL_COUNT (VSM_POOL_PAGES * VSM_POOL_PAGES)

#define VSM_PHYS_MASK 0x0FFFu
#define VSM_VALID 0x1000u
#define VSM_DIRTY 0x2000u

// Light space: x and y across the sun's view, z towards the sun.
struct VsmBasis {
    vec3 right;
    vec3 up;
    vec3 toSun;
};

VsmBasis vsmBasis() {
    vec3 L = -normalize(showroom.sunDirection.xyz);
    vec3 right = normalize(cross(abs(L.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), L));
    return VsmBasis(right, cross(L, right), L);
}

vec3 vsmLightSpace(VsmBasis b, vec3 p) {
    return vec3(dot(p, b.right), dot(p, b.up), dot(p, b.toSun));
}

float vsmTexelSize(int level) {
    return showroom.vsmFirstLevel * exp2(float(level)) / float(VSM_LEVEL_SIZE);
}

float vsmPageWorld(int level) {
    return vsmTexelSize(level) * float(VSM_PAGE_SIZE);
}

// Absolute coordinate of the level's first page: its window is the 32 x 32
// pages around the camera.
ivec2 vsmWindowOrigin(VsmBasis b, int level) {
    vec2 camera = vsmLightSpace(b, showroom.cameraPosition.xyz).xy;
    return ivec2(floor(camera / vsmPageWorld(level))) - VSM_PAGES / 2;
}

bool vsmInWindow(ivec2 page, ivec2 origin) {
    ivec2 local = page - origin;
    return all(greaterThanEqual(local, ivec2(0))) && all(lessThan(local, ivec2(VSM_PAGES)));
}

// Where an absolute page lives in the table images.
ivec2 vsmEntryCoord(int level, ivec2 page) {
    ivec2 wrapped = page & (VSM_PAGES - 1);
    return ivec2(wrapped.x, level * VSM_PAGES + wrapped.y);
}

ivec2 vsmEntryCoordOf(uint entry) {
    return ivec2(int(entry) % VSM_PAGES, int(entry) / VSM_PAGES);
}

uint vsmTag(ivec2 page) {
    return uint(page.x & 0xFFFF) | (uint(page.y & 0xFFFF) << 16);
}

// The absolute page a table slot stands for in this frame's window.
ivec2 vsmPageOfSlot(ivec2 origin, ivec2 slot) {
    return origin + ((slot - origin) & (VSM_PAGES - 1));
}

ivec2 vsmPhysicalPage(uint entry) {
    uint p = entry & VSM_PHYS_MASK;
    return ivec2(int(p) % VSM_POOL_PAGES, int(p) / VSM_POOL_PAGES);
}

// Depth in the map, 0 nearest the sun. Absolute along the sun's axis, so it
// does not change as the camera moves: it covers casters and receivers within
// vsmDepthRange of the world origin along it.
float vsmDepth(float towardsSun) {
    return clamp(0.5 - towardsSun / (2.0 * showroom.vsmDepthRange), 0.0, 1.0);
}

// The finest level whose texels are no larger than a pixel of `pixelWorld`
// world units (shifted by vsmLevelBias) and whose window holds `worldPos`;
// -1 when none does.
int vsmSelectLevel(VsmBasis b, vec3 worldPos, float pixelWorld) {
    float ideal = log2(max(pixelWorld / vsmTexelSize(0), 1e-6)) + showroom.vsmLevelBias;
    int level = clamp(int(ceil(ideal)), 0, VSM_LEVELS - 1);
    vec2 ls = vsmLightSpace(b, worldPos).xy;
    for (; level < VSM_LEVELS; ++level) {
        ivec2 page = ivec2(floor(ls / vsmPageWorld(level)));
        if (vsmInWindow(page, vsmWindowOrigin(b, level))) return level;
    }
    return -1;
}

// World size of a pixel at a view depth, from a projection matrix and the
// screen height.
float vsmPixelWorld(float viewDepth, mat4 proj, float screenHeight) {
    return 2.0 * viewDepth / (abs(proj[1][1]) * screenHeight);
}

// Lookup position moved off the surface along its normal by a texel or so of
// the level, more where the sun grazes it.
vec3 vsmOffsetPosition(vec3 worldPos, vec3 N, vec3 L, int level) {
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float sinL = sqrt(max(1.0 - NdotL * NdotL, 0.0));
    return worldPos + N * (vsmTexelSize(level) * showroom.vsmNormalBias * (0.5 + sinL));
}

#endif
