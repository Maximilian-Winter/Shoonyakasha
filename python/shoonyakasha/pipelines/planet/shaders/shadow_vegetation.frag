#version 450
//
// The vegetation's shadow: depth only, with the foliage's alpha test
// (foliage.glsl): cards and grass against the foliage atlas, impostors
// against theirs, a cut-out scan against its cell of the scans' atlas;
// bark, plain parts and other scans always cast.
//

#define FOLIAGE_SET 2
#include "foliage.glsl"

layout(location = 0) flat in int fragPart;
layout(location = 1) in vec2 fragUv;
layout(location = 2) flat in vec4 fragCell;

const int PART_CARD = 1;
const int PART_GRASS = 2;
const int PART_IMPOSTOR = 3;
const int PART_SCAN = 5;

void main() {
    bool drawn = true;
    if (fragPart == PART_CARD || fragPart == PART_GRASS) {
        foliageTexel(vegetationAtlas, fragUv, drawn);
    } else if (fragPart == PART_IMPOSTOR) {
        foliageTexel(vegetationImpostor, fragUv, drawn);
    } else if (fragPart >= PART_SCAN && ((fragPart - PART_SCAN) & 2) != 0) {
        drawn = scanTexel(scanAlbedo, fragCell, fragUv).a >= FOLIAGE_CUTOFF;
    }
    if (!drawn) discard;
}
