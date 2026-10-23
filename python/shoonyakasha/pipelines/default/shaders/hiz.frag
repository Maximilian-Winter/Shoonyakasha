#version 450
// The farthest depth of the block of gDepth under each texel of the
// occlusion image `hiz` (pass HiZ). The block is rounded outwards, so every
// depth texel is in the block of each hiz texel that overlaps it; the CPU
// reads hiz back and tests bounds against it (execution "occlusion").

layout(set = 0, binding = 0) uniform sampler2D gDepth;

layout(push_constant) uniform HiZPush {
    vec2 extent;   // hiz's size
} push;

layout(location = 0) out float outDepth;

void main() {
    ivec2 source = textureSize(gDepth, 0);
    vec2 scale = vec2(source) / push.extent;
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 first = ivec2(floor(vec2(texel) * scale));
    ivec2 last = min(ivec2(ceil(vec2(texel + 1) * scale)), source) - 1;
    float farthest = 0.0;
    for (int y = first.y; y <= last.y; ++y) {
        for (int x = first.x; x <= last.x; ++x) {
            farthest = max(farthest, texelFetch(gDepth, ivec2(x, y), 0).r);
        }
    }
    outDepth = farthest;
}
