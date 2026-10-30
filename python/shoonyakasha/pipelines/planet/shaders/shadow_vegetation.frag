#version 450
//
// The vegetation's shadow: depth only, with the foliage's alpha test
// (foliage.glsl): cards and grass against the foliage atlas, impostors
// against theirs; bark and plain parts always cast.
//

#define FOLIAGE_SET 2
#include "foliage.glsl"

layout(location = 0) flat in int fragPart;
layout(location = 1) in vec2 fragUv;

const int PART_CARD = 1;
const int PART_GRASS = 2;
const int PART_IMPOSTOR = 3;

void main() {
    bool drawn = true;
    if (fragPart == PART_CARD || fragPart == PART_GRASS) {
        foliageTexel(vegetationAtlas, fragUv, drawn);
    } else if (fragPart == PART_IMPOSTOR) {
        foliageTexel(vegetationImpostor, fragUv, drawn);
    }
    if (!drawn) discard;
}
