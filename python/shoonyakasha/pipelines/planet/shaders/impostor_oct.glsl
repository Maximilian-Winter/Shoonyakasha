// The trees' octahedral impostors (shoonyakasha.planet.vegetation_impostors):
// each tree drawn from OCT_VIEWS x OCT_VIEWS directions over the upper
// hemisphere, OCT_TEXELS texels square each, into a block of the atlas.
// View (i, j) looks at the tree from the direction octDirection(i, j), the
// hemi-octahedral decoding of the middle of grid cell (i, j), y up in the
// tree's frame; it is framed by the tree's bounding sphere (centre (0, y,
// 0), radius r, in the tree's units) in the frame octBasis gives: across
// `right`, up `up`, a texel's (x, y) in -1 .. 1 times the radius from the
// centre, y up (the texel's row counts down).

const int OCT_VIEWS = 6;
const int OCT_TEXELS = 96;

// The grid position (0 .. 1 each) of unit direction `d` with d.y >= 0.
vec2 octEncode(vec3 d) {
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    return vec2(d.x + d.z, d.x - d.z) * 0.5 + 0.5;
}

// The unit direction at grid position `uv` (0 .. 1 each).
vec3 octDecode(vec2 uv) {
    vec2 f = uv * 2.0 - 1.0;
    vec3 d = vec3((f.x + f.y) * 0.5, 0.0, (f.x - f.y) * 0.5);
    d.y = 1.0 - abs(d.x) - abs(d.z);
    return normalize(d);
}

vec3 octDirection(ivec2 view) {
    return octDecode((vec2(view) + 0.5) / float(OCT_VIEWS));
}

// The frame of the view from direction `d`: its right and its up.
void octBasis(vec3 d, out vec3 right, out vec3 up) {
    vec3 r = cross(vec3(0.0, 1.0, 0.0), d);
    right = dot(r, r) < 1e-6 ? vec3(1.0, 0.0, 0.0) : normalize(r);
    up = cross(d, right);
}

// The texel coordinates (0 .. 1) within a tree's block of view `view` at
// the view's (x, y) in -1 .. 1.
vec2 octBlockUv(ivec2 view, vec2 xy) {
    return (vec2(view) + vec2(xy.x * 0.5 + 0.5, 0.5 - xy.y * 0.5)) / float(OCT_VIEWS);
}

// The three views nearest unit direction `d` (d.y >= 0), as the corners of
// the triangle of the views' grid holding it, and their weights.
void octNearest(vec3 d, out ivec2 views[3], out vec3 weights) {
    vec2 g = clamp(octEncode(d) * float(OCT_VIEWS) - 0.5, vec2(0.0), vec2(float(OCT_VIEWS - 1)));
    vec2 base = min(floor(g), vec2(float(OCT_VIEWS - 2)));
    vec2 f = g - base;
    ivec2 b = ivec2(base);
    if (f.x + f.y <= 1.0) {
        views[0] = b;
        views[1] = b + ivec2(1, 0);
        views[2] = b + ivec2(0, 1);
        weights = vec3(1.0 - f.x - f.y, f.x, f.y);
    } else {
        views[0] = b + ivec2(1, 1);
        views[1] = b + ivec2(0, 1);
        views[2] = b + ivec2(1, 0);
        weights = vec3(f.x + f.y - 1.0, 1.0 - f.x, 1.0 - f.y);
    }
}
