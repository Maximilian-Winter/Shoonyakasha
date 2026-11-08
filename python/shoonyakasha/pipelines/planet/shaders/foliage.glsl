// The vegetation's textures (vegetationTextureSet in pipeline.json, bound as
// set FOLIAGE_SET): the foliage atlas, the bark tile and the trees'
// impostors with their normals (shoonyakasha.planet.vegetation_textures and
// vegetation_impostors), the scans' albedo and normal atlases
// (shoonyakasha.planet.scans.atlas), and the alpha test the vegetation's
// fragment shaders share. impostorTexel blends a tree's octahedral views
// (impostor_oct.glsl).

#include "impostor_oct.glsl"

layout(set = FOLIAGE_SET, binding = 0) uniform sampler2D vegetationAtlas;
layout(set = FOLIAGE_SET, binding = 1) uniform sampler2D vegetationBark;
layout(set = FOLIAGE_SET, binding = 2) uniform sampler2D vegetationImpostor;
layout(set = FOLIAGE_SET, binding = 3) uniform sampler2D vegetationImpostorNormal;
layout(set = FOLIAGE_SET, binding = 4) uniform sampler2D scanAlbedo;
layout(set = FOLIAGE_SET, binding = 5) uniform sampler2D scanNormal;

// Mip levels a scan's texel is read at, at most, so its cell's neighbours
// do not bleed into it.
const float SCAN_MAX_LOD = 5.0;

// The texel of a scan's material at its own `uv` (wrapped into its cell
// `rect`: u0, v0, width, height), its level from the unwrapped (u, v)'s
// rate of change, at most SCAN_MAX_LOD.
vec4 scanTexel(sampler2D image, vec4 rect, vec2 uv) {
    vec2 size = vec2(textureSize(image, 0)) * rect.zw;
    vec2 dx = dFdx(uv) * size, dy = dFdy(uv) * size;
    float level = clamp(0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8)), 0.0, SCAN_MAX_LOD);
    vec2 inset = 0.5 / size;
    return textureLod(image, rect.xy + clamp(fract(uv), inset, 1.0 - inset) * rect.zw, level);
}

// A coverage of 1/2 or more is drawn.
const float FOLIAGE_CUTOFF = 0.5;
// Coverage times 1 + this per mip level: the levels' averaged coverage
// shrinks as the cards' gaps blend in, which would thin the foliage with
// distance.
const float FOLIAGE_MIP_GAIN = 0.25;

// An impostor's texel at `local` (the point from its sphere's centre in the
// tree's frame, in radii) seen from `view` (in the tree's frame; at least
// IMPOSTOR_LEAST above level), its tree's block at `block` (u0, v0): the
// three views nearest `view` (octNearest), each sampled where `local`
// falls in it (none outside it), weighted, in `albedo` and `normals`;
// `one` takes the nearest view alone. Whether it is drawn as foliageTexel
// tells.
const float IMPOSTOR_LEAST = 0.05;

void impostorTexel(vec3 local, vec3 view, vec2 block, bool one, out vec4 albedo, out vec4 normals, out bool drawn) {
    vec3 d = normalize(vec3(view.x, max(view.y, IMPOSTOR_LEAST), view.z));
    ivec2 views[3];
    vec3 weights;
    octNearest(d, views, weights);
    if (one) {
        int k = weights.x >= weights.y && weights.x >= weights.z ? 0 : weights.y >= weights.z ? 1 : 2;
        views[0] = views[k];
        weights = vec3(1.0, 0.0, 0.0);
    }
    vec2 size = vec2(float(OCT_VIEWS * OCT_TEXELS)) / vec2(textureSize(vegetationImpostor, 0));
    albedo = vec4(0.0);
    normals = vec4(0.0);
    float level = 0.0;
    for (int k = 0; k < 3; ++k) {
        vec3 right, up;
        octBasis(octDirection(views[k]), right, up);
        vec2 xy = vec2(dot(local, right), dot(local, up));
        float inside = step(max(abs(xy.x), abs(xy.y)), 1.0);
        vec2 uv = block + octBlockUv(views[k], clamp(xy, -1.0, 1.0)) * size;
        vec4 a = texture(vegetationImpostor, uv);
        albedo += weights[k] * vec4(a.rgb, a.a * inside);
        normals += weights[k] * texture(vegetationImpostorNormal, uv);
        if (k == 0) level = max(textureQueryLod(vegetationImpostor, uv).x, 0.0);
    }
    albedo.a *= 1.0 + FOLIAGE_MIP_GAIN * level;
    drawn = albedo.a >= FOLIAGE_CUTOFF;
}

// The texel of `image` at `uv` and whether it is drawn, its alpha scaled
// up with the mip level.
vec4 foliageTexel(sampler2D image, vec2 uv, out bool drawn) {
    vec4 texel = texture(image, uv);
    float level = max(textureQueryLod(image, uv).x, 0.0);
    texel.a *= 1.0 + FOLIAGE_MIP_GAIN * level;
    drawn = texel.a >= FOLIAGE_CUTOFF;
    return texel;
}
