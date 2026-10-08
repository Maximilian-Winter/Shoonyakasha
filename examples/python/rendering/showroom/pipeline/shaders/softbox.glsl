// softbox.glsl - what a softbox's diffuser looks like: brighter in the middle
// than at the edges (the hotspot of the lamp behind the fabric), with an
// optional fabric grid over it. One definition for the light the panel
// casts (area_lights.glsl, blurred as each surface's reflection blurs it)
// and for the panel itself (gbuffer_body.glsl, on materials whose
// emissiveFactor.a marks them as a diffuser).
//
// The pattern averages to 1 over the panel, so it moves light around the
// panel without changing how much there is. Both parts are box-filtered
// analytically, over any footprint, so no texture or mip chain is needed:
// a sharp reflection sees the grid, a rough one the hotspot, a very rough
// one the panel's average.

#ifndef SHOWROOM_SOFTBOX_GLSL
#define SHOWROOM_SOFTBOX_GLSL

const float SOFTBOX_HOTSPOT = 0.45;       // the corners are this much darker than the centre
const float SOFTBOX_LINE = 0.12;          // the grid's strips, as a fraction of a cell
const float SOFTBOX_LINE_LIGHT = 0.0;     // the light the strips let through: none, they are black fabric

// The fraction of [p - w/2, p + w/2] covered by strips SOFTBOX_LINE wide
// at every integer (Quilez's filtered grid), per axis.
vec2 softboxLineCoverage(vec2 p, vec2 w) {
    w = max(w, vec2(1e-4));
    vec2 a = p + 0.5 * w;
    vec2 b = p - 0.5 * w;
    vec2 covered = (floor(a) * SOFTBOX_LINE + min(fract(a), SOFTBOX_LINE))
                 - (floor(b) * SOFTBOX_LINE + min(fract(b), SOFTBOX_LINE));
    return clamp(covered / w, 0.0, 1.0);
}

// The diffuser at uv (0 to 1 across the panel), averaged over a box `width`
// across (in uv). `cells` is the grid's cells along u and v, 0 for no grid.
float softboxPattern(vec2 uv, vec2 width, vec2 cells) {
    uv = clamp(uv, 0.0, 1.0);
    // Hotspot: 1 - k (x^2 + y^2) / 2 over x, y in -1..1. A box filter adds
    // its half-width squared over 3 to x^2; past the panel's size the
    // footprint sees the panel's average instead.
    vec2 x = uv * 2.0 - 1.0;
    vec2 half_ = width;                       // the box's half-width in x, y units
    float hotMean = 1.0 - SOFTBOX_HOTSPOT / 3.0;
    float hot = 1.0 - SOFTBOX_HOTSPOT * 0.5 * dot(x * x + half_ * half_ / 3.0, vec2(1.0));
    hot = mix(hot, hotMean, clamp(max(width.x, width.y), 0.0, 1.0));
    float pattern = hot / hotMean;

    if (cells.x > 0.0) {
        // Strips centred on the cell borders, the panel's edges among them.
        vec2 coverage = softboxLineCoverage(uv * cells + SOFTBOX_LINE * 0.5, width * cells);
        float lines = 1.0 - (1.0 - coverage.x) * (1.0 - coverage.y);
        float area = 1.0 - (1.0 - SOFTBOX_LINE) * (1.0 - SOFTBOX_LINE);
        float gridMean = mix(1.0, SOFTBOX_LINE_LIGHT, area);
        pattern *= mix(1.0, SOFTBOX_LINE_LIGHT, lines) / gridMean;
    }
    return pattern;
}

// Grid cells along a panel of half-sizes `halfSize`, with `across` cells
// along its short side: square-ish cells whatever the panel's shape.
vec2 softboxCells(vec2 halfSize, float across) {
    if (across < 0.5) return vec2(0.0);
    float side = max(min(halfSize.x, halfSize.y), 1e-4);
    return max(floor(halfSize / side * across + 0.5), vec2(1.0));
}

#endif
