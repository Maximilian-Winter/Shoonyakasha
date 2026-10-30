// The vegetation's textures (vegetationTextureSet in pipeline.json, bound as
// set FOLIAGE_SET): the foliage atlas, the bark tile and the trees'
// impostors with their normals (shoonyakasha.planet.vegetation_textures and
// vegetation_impostors), and the alpha test the vegetation's fragment
// shaders share.

layout(set = FOLIAGE_SET, binding = 0) uniform sampler2D vegetationAtlas;
layout(set = FOLIAGE_SET, binding = 1) uniform sampler2D vegetationBark;
layout(set = FOLIAGE_SET, binding = 2) uniform sampler2D vegetationImpostor;
layout(set = FOLIAGE_SET, binding = 3) uniform sampler2D vegetationImpostorNormal;

// A coverage of 1/2 or more is drawn.
const float FOLIAGE_CUTOFF = 0.5;
// Coverage times 1 + this per mip level: the levels' averaged coverage
// shrinks as the cards' gaps blend in, which would thin the foliage with
// distance.
const float FOLIAGE_MIP_GAIN = 0.25;

// The texel of `image` at `uv` and whether it is drawn, its alpha scaled
// up with the mip level.
vec4 foliageTexel(sampler2D image, vec2 uv, out bool drawn) {
    vec4 texel = texture(image, uv);
    float level = max(textureQueryLod(image, uv).x, 0.0);
    texel.a *= 1.0 + FOLIAGE_MIP_GAIN * level;
    drawn = texel.a >= FOLIAGE_CUTOFF;
    return texel;
}
