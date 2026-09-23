#version 450
//
// Procedural mandala layers. Every entity drawn with this shader is one quad;
// shape.x picks which layer is painted on it and shape.yz is the quad size in
// world units, so all layers share world-unit coordinates centred on their
// quad and line up when stacked. Edges are antialiased from signed distances.
//
// scene.deity selects the palette of every layer and the symbol at the centre.
//
// Output is premultiplied alpha; temple_pipeline.json blends it with
// (one, one_minus_src_alpha) for solid layers and (one, one) for glows.
//

#include "sk/noise.glsl"
#include "sk/shapes2d.glsl"

layout(set = 0, binding = 0) uniform TempleSceneUBO {
    mat4 viewProjection;
    vec2 resolution;
    float time;
    float deity;       // which mandala is shown, see temple.py DEITIES
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 tintColor;
    vec4 shape;        // x = layer id, yz = quad size in world units, w = per-entity seed
} push;

layout(location = 0) in vec2 fragLocal;

layout(location = 0) out vec4 outColor;

const float TAU = SK_TAU;

const int LAYER_SKY      = 0;
const int LAYER_FIRE     = 1;
const int LAYER_VAJRA    = 2;
const int LAYER_PETALS   = 3;
const int LAYER_PALACE   = 4;
const int LAYER_LOTUS    = 5;
const int LAYER_CENTRE   = 6;
const int LAYER_GLOW     = 7;
const int LAYER_VIGNETTE = 8;
const int LAYER_LAMP     = 9;

const int DEITY_VAJRAYOGINI = 0;
const int DEITY_GREEN_TARA  = 1;
const int DEITY_WHITE_TARA  = 2;
const int DEITY_VAJRAPANI   = 3;

const vec3 GOLD      = vec3(1.00, 0.74, 0.28);
const vec3 DEEP_GOLD = vec3(0.55, 0.33, 0.08);
const vec3 BONE      = vec3(1.00, 0.94, 0.84);
const vec3 MOON      = vec3(0.90, 0.93, 1.00);

// Colours of the four quarters, one per direction.
const vec3 EAST_BLUE    = vec3(0.10, 0.20, 0.62);
const vec3 SOUTH_YELLOW = vec3(0.86, 0.60, 0.08);
const vec3 WEST_RED     = vec3(0.68, 0.06, 0.07);
const vec3 NORTH_GREEN  = vec3(0.05, 0.44, 0.24);

// ── Palettes ────────────────────────────────────────────────────

struct Palette {
    vec3 nebula;       // the two nebula colours in the sky
    vec3 nebula2;
    vec3 flameBase;    // fire ring, from the root of a flame to its tip
    vec3 flameBody;
    vec3 flameTip;
    float rainbow;     // 0..1, how far the fire's hue turns with the angle
    vec3 fence;        // band behind the vajras
    vec3 petalA;       // alternating petals of the outer lotus ring
    vec3 petalB;
    vec3 petalBand;
    vec3 lotusDeep;    // central lotus, front row from base to tip
    vec3 lotusTip;
    vec3 lotusBack;    // central lotus, back row at its tip
    vec3 lotusRim;
    vec3 courtyard;
};

Palette paletteFor(int deity) {
    Palette p;
    if (deity == DEITY_GREEN_TARA) {
        p.nebula    = vec3(0.02, 0.22, 0.12);
        p.nebula2   = vec3(0.02, 0.08, 0.22);
        p.flameBase = vec3(0.00, 0.18, 0.08);
        p.flameBody = vec3(0.08, 0.70, 0.28);
        p.flameTip  = vec3(0.80, 1.00, 0.50);
        p.rainbow   = 0.0;
        p.fence     = vec3(0.01, 0.10, 0.08);
        p.petalA    = vec3(0.25, 0.80, 0.45);
        p.petalB    = vec3(0.35, 0.62, 0.95);
        p.petalBand = vec3(0.01, 0.09, 0.05);
        p.lotusDeep = vec3(0.01, 0.16, 0.07);
        p.lotusTip  = vec3(0.50, 0.95, 0.62);
        p.lotusBack = vec3(0.04, 0.34, 0.15);
        p.lotusRim  = vec3(0.80, 1.00, 0.82);
        p.courtyard = vec3(0.01, 0.05, 0.05);
    } else if (deity == DEITY_WHITE_TARA) {
        p.nebula    = vec3(0.12, 0.14, 0.28);
        p.nebula2   = vec3(0.22, 0.12, 0.20);
        p.flameBase = vec3(0.30, 0.34, 0.60);
        p.flameBody = vec3(0.70, 0.80, 1.00);
        p.flameTip  = vec3(1.00, 1.00, 1.00);
        p.rainbow   = 0.55;
        p.fence     = vec3(0.07, 0.09, 0.24);
        p.petalA    = vec3(0.95, 0.95, 1.00);
        p.petalB    = vec3(0.95, 0.82, 0.55);
        p.petalBand = vec3(0.08, 0.08, 0.18);
        p.lotusDeep = vec3(0.40, 0.42, 0.58);
        p.lotusTip  = vec3(1.00, 1.00, 1.00);
        p.lotusBack = vec3(0.55, 0.58, 0.80);
        p.lotusRim  = vec3(1.00, 0.95, 0.80);
        p.courtyard = vec3(0.04, 0.05, 0.12);
    } else if (deity == DEITY_VAJRAPANI) {
        p.nebula    = vec3(0.02, 0.05, 0.32);
        p.nebula2   = vec3(0.16, 0.02, 0.22);
        p.flameBase = vec3(0.02, 0.02, 0.30);
        p.flameBody = vec3(0.08, 0.32, 1.00);
        p.flameTip  = vec3(0.75, 0.95, 1.00);
        p.rainbow   = 0.0;
        p.fence     = vec3(0.01, 0.02, 0.09);
        p.petalA    = vec3(0.20, 0.35, 0.95);
        p.petalB    = vec3(0.95, 0.60, 0.18);
        p.petalBand = vec3(0.02, 0.02, 0.10);
        p.lotusDeep = vec3(0.02, 0.03, 0.25);
        p.lotusTip  = vec3(0.40, 0.62, 1.00);
        p.lotusBack = vec3(0.08, 0.12, 0.45);
        p.lotusRim  = vec3(0.70, 0.85, 1.00);
        p.courtyard = vec3(0.01, 0.01, 0.05);
    } else {
        p.nebula    = vec3(0.30, 0.03, 0.10);
        p.nebula2   = vec3(0.04, 0.05, 0.22);
        p.flameBase = vec3(0.45, 0.02, 0.02);
        p.flameBody = vec3(0.95, 0.24, 0.03);
        p.flameTip  = vec3(1.00, 0.72, 0.30);
        p.rainbow   = 0.0;
        p.fence     = vec3(0.03, 0.06, 0.20);
        p.petalA    = vec3(0.92, 0.28, 0.40);
        p.petalB    = vec3(0.98, 0.52, 0.30);
        p.petalBand = vec3(0.16, 0.01, 0.05);
        p.lotusDeep = vec3(0.32, 0.004, 0.02);
        p.lotusTip  = vec3(0.95, 0.26, 0.24);
        p.lotusBack = vec3(0.50, 0.03, 0.07);
        p.lotusRim  = vec3(1.00, 0.80, 0.78);
        p.courtyard = vec3(0.05, 0.02, 0.07);
    }
    return p;
}

// ── Shapes ──────────────────────────────────────────────────────

vec3 hue(float h) {
    return clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}

// Vajra lying along x, centred on its hub, about 0.5 long.
float sdVajra(vec2 q) {
    vec2 m = vec2(abs(q.x), q.y);
    float hub = length(q) - 0.045;
    float bulb = sdEllipse(m - vec2(0.105, 0.0), vec2(0.06, 0.055));
    float centreProng = sdEllipse(m - vec2(0.195, 0.0), vec2(0.055, 0.016));
    float sideProngs = sdEllipse(vec2(m.x, abs(m.y)) - vec2(0.18, 0.052), vec2(0.05, 0.014));
    return min(min(hub, bulb), min(centreProng, sideProngs));
}

// ── Layers ──────────────────────────────────────────────────────

vec4 sky(vec2 p, Palette pal) {
    float r = length(p);
    vec3 col = mix(pal.nebula * 0.3, vec3(0.008, 0.006, 0.02), smoothstep(0.0, 9.0, r));

    float drift = scene.time * 0.015;
    col += pal.nebula * pow(fbm(p * 0.30 + vec2(drift, 0.0)), 3.0);
    col += pal.nebula2 * pow(fbm(p * 0.45 - vec2(3.1, drift)), 3.5);

    vec2 grid = p * 5.0;
    vec2 cell = floor(grid);
    float h = hash(cell);
    if (h > 0.92) {
        vec2 offset = vec2(hash(cell + vec2(57.0, 113.0)), hash(cell + vec2(211.0, 37.0))) - 0.5;
        float d = length(fract(grid) - 0.5 - offset * 0.6);
        float twinkle = 0.55 + 0.45 * sin(scene.time * (0.7 + 2.5 * h) + h * 40.0);
        col += vec3(1.0, 0.88, 0.78) * smoothstep(0.10, 0.0, d) * twinkle * (h - 0.92) * 11.0;
    }
    return vec4(col, 1.0);
}

vec4 fireRing(vec2 p, Palette pal) {
    float r = length(p);
    vec4 acc = vec4(0.0);
    if (r < 3.8 || r > 4.9) return acc;

    vec2 dir = p / r;
    float a = atan(p.y, p.x);
    float v = (r - 3.95) / 0.75;
    float n = fbm(p * 1.7 - dir * scene.time * 1.4);
    float tongues = 0.22 * cos(a * 36.0 + n * 7.0 - scene.time * 0.8);
    float heat = n * 1.25 + tongues - v * 1.05 - 0.15;

    float t = clamp(heat * 1.6, 0.0, 1.0);
    vec3 flame = mix(pal.flameBase, pal.flameBody, smoothstep(0.0, 0.45, t));
    flame = mix(flame, pal.flameTip, smoothstep(0.6, 1.0, t));
    vec3 spectrum = hue(a / TAU + scene.time * 0.03) * (0.4 + 0.6 * t) + 0.25 * t;
    flame = mix(flame, spectrum, pal.rainbow * (1.0 - smoothstep(0.7, 1.0, t)));
    float flameAlpha = smoothstep(0.0, 0.10, heat) * fill(3.9 - r);
    over(acc, flame, flameAlpha);

    // Base of the fire: a solid band with a gold inner edge.
    over(acc, pal.flameBase * 1.1, fill(abs(r - 3.93) - 0.06));
    over(acc, GOLD, stroke(r - 3.87, 0.012));
    return acc;
}

vec4 vajraRing(vec2 p, Palette pal) {
    float r = length(p);
    vec4 acc = vec4(0.0);
    if (r < 3.4 || r > 3.95) return acc;

    over(acc, pal.fence, fill(abs(r - 3.665) - 0.195));
    over(acc, GOLD, stroke(r - 3.47, 0.012));
    over(acc, GOLD, stroke(r - 3.86, 0.012));

    // Vajras laid end to end around the ring, x along the ring, y across it.
    const float COUNT = 40.0;
    vec2 f = foldPolar(p, COUNT);
    vec2 q = vec2(atan(f.y, f.x) * r, r - 3.665);
    float vajra = sdVajra(q);

    over(acc, mix(DEEP_GOLD, GOLD, 0.35 + 0.65 * smoothstep(-0.06, 0.06, q.y)), fill(vajra));
    over(acc, DEEP_GOLD * 0.6, stroke(vajra, 0.004));
    over(acc, BONE, fill(length(q) - 0.016));
    return acc;
}

vec4 petalRing(vec2 p, Palette pal) {
    float r = length(p);
    vec4 acc = vec4(0.0);
    if (r < 2.95 || r > 3.55) return acc;

    over(acc, pal.petalBand, fill(abs(r - 3.25) - 0.23));

    const float COUNT = 32.0;
    float sector = TAU / COUNT;
    float index = floor((atan(p.y, p.x) + 0.5 * sector) / sector);
    vec2 q = foldPolar(p, COUNT);
    float d = sdPetal(q - vec2(3.26, 0.0), 0.19, 0.085);

    vec3 base = mod(index, 2.0) < 0.5 ? pal.petalA : pal.petalB;
    vec3 col = mix(base * 0.55, mix(base, BONE, 0.35), smoothstep(3.08, 3.42, q.x));
    over(acc, col, fill(d));
    over(acc, pal.petalBand * 2.0, stroke(d, 0.005));
    over(acc, GOLD, stroke(r - 3.03, 0.01));
    return acc;
}

float palaceOutline(vec2 p) {
    const float H = 2.15;
    float body = sdBox(p, vec2(H));
    vec2 q = abs(p);
    if (q.y > q.x) q = q.yx;
    float porch = sdBox(q - vec2(H + 0.16, 0.0), vec2(0.16, 0.42));
    float lintel = sdBox(q - vec2(H + 0.36, 0.0), vec2(0.07, 0.64));
    return min(body, min(porch, lintel));
}

vec4 palace(vec2 p, Palette pal) {
    vec4 acc = vec4(0.0);
    float outline = palaceOutline(p);
    if (outline > 0.05) return acc;

    // Five walls, outermost first, as successively inset copies of the outline.
    const float WALL = 0.042;
    vec3 walls[5] = vec3[5](BONE, SOUTH_YELLOW, WEST_RED, NORTH_GREEN, EAST_BLUE);
    for (int i = 0; i < 5; i++) {
        float d = outline + float(i) * WALL;
        over(acc, walls[i], fill(d));
        over(acc, vec3(0.02), stroke(d, 0.003));
    }

    // Ground inside the walls: four triangular quarters meeting at the centre.
    vec2 a = abs(p);
    vec3 horizontal = p.x > 0.0 ? NORTH_GREEN : SOUTH_YELLOW;
    vec3 vertical = p.y > 0.0 ? WEST_RED : EAST_BLUE;
    vec3 ground = mix(horizontal, vertical, fill(a.x - a.y));
    ground *= 0.62 + 0.25 * smoothstep(2.3, 0.4, max(a.x, a.y));
    float inner = outline + 5.0 * WALL;
    over(acc, ground, fill(inner));
    over(acc, GOLD, stroke(inner, 0.008));
    over(acc, GOLD * 0.8, stroke((a.x - a.y) * 0.7071, 0.006) * fill(inner));

    // Circular courtyard for the lotus, ringed with pearls.
    float r = length(p);
    over(acc, pal.courtyard, fill(r - 1.86));
    over(acc, GOLD, stroke(r - 1.86, 0.012));
    over(acc, GOLD, stroke(r - 1.74, 0.006));
    vec2 pearl = foldPolar(p, 48.0);
    over(acc, BONE, fill(length(pearl - vec2(1.80, 0.0)) - 0.03));
    return acc;
}

vec4 lotus(vec2 p, Palette pal) {
    vec4 acc = vec4(0.0);
    if (length(p) > 1.75) return acc;

    // Back row, turned half a sector so it shows between the front petals.
    vec2 back = foldPolar(rotate(p, TAU / 16.0), 8.0);
    float db = sdPetal(back - vec2(1.07, 0.0), 0.62, 0.33);
    over(acc, mix(pal.lotusDeep * 0.4, pal.lotusBack, smoothstep(0.5, 1.6, back.x)), fill(db));
    over(acc, pal.lotusRim * 0.9, stroke(db, 0.006));

    vec2 front = foldPolar(p, 8.0);
    float d = sdPetal(front - vec2(0.96, 0.0), 0.62, 0.36);
    vec3 col = mix(pal.lotusDeep, pal.lotusTip, smoothstep(0.45, 1.6, front.x));
    col = mix(col, BONE, 0.35 * stroke(front.y, 0.008) * smoothstep(1.5, 0.6, front.x));
    over(acc, col, fill(d));
    over(acc, pal.lotusRim, stroke(d, 0.008));

    over(acc, GOLD, fill(length(p) - 0.46));
    return acc;
}

// A disc on which a deity is seated, with faint rays and a gold rim.
void seatDisc(inout vec4 acc, vec2 p, vec3 inner, vec3 outer, float radius) {
    float r = length(p);
    over(acc, vec3(0.0), 0.45 * smoothstep(radius + 0.10, radius - 0.02, r));
    vec3 col = mix(inner, outer, smoothstep(0.0, radius, r));
    col *= 0.92 + 0.08 * cos(atan(p.y, p.x) * 24.0);
    over(acc, col, fill(r - radius));
    over(acc, GOLD, stroke(r - radius, 0.012));
}

// Vajrayogini: the red dharmodaya, two interlocking triangles with a joy-swirl.
vec4 dharmodaya(vec2 p) {
    vec4 acc = vec4(0.0);
    float r = length(p);
    if (r > 1.1) return acc;

    float up = sdTriangle(p, 0.84);
    float down = sdTriangle(vec2(p.x, -p.y), 0.84);

    over(acc, vec3(0.0), 0.45 * smoothstep(0.10, -0.02, min(up, down)));

    over(acc, mix(vec3(0.80, 0.03, 0.01), vec3(0.22, 0.0, 0.01), smoothstep(0.0, 0.9, r)), fill(up));
    over(acc, GOLD, stroke(up, 0.012));
    over(acc, BONE, 0.5 * stroke(up + 0.07, 0.004));

    over(acc, mix(vec3(0.95, 0.06, 0.02), vec3(0.36, 0.0, 0.01), smoothstep(0.0, 0.9, r)), fill(down));
    over(acc, GOLD, stroke(down, 0.012));
    over(acc, BONE, 0.5 * stroke(down + 0.07, 0.004));

    // Joy-swirl at the centre, four arms turning inward.
    float a = atan(p.y, p.x);
    float arms = sin(a * 4.0 + r * 16.0 - scene.time * 1.2);
    float armMask = clamp(arms / max(fwidth(arms), 1e-5) * 0.5 + 0.5, 0.0, 1.0);
    over(acc, mix(vec3(0.55, 0.0, 0.01), BONE, armMask), fill(r - 0.28));
    over(acc, GOLD, stroke(r - 0.28, 0.01));
    over(acc, BONE, fill(r - 0.05));
    return acc;
}

// Green Tara: a blue utpala flower resting on a moon disc.
vec4 utpala(vec2 p) {
    vec4 acc = vec4(0.0);
    if (length(p) > 1.1) return acc;

    seatDisc(acc, p, MOON, vec3(0.55, 0.62, 0.80), 0.92);

    vec2 outer = foldPolar(p, 8.0);
    float d1 = sdPetal(outer - vec2(0.50, 0.0), 0.36, 0.11);
    over(acc, mix(vec3(0.02, 0.10, 0.45), vec3(0.45, 0.70, 1.00), smoothstep(0.2, 0.85, outer.x)), fill(d1));
    over(acc, vec3(0.75, 0.88, 1.0), stroke(d1, 0.005));

    vec2 inner = foldPolar(rotate(p, TAU / 16.0), 8.0);
    float d2 = sdPetal(inner - vec2(0.32, 0.0), 0.24, 0.085);
    over(acc, mix(vec3(0.05, 0.18, 0.62), vec3(0.62, 0.82, 1.00), smoothstep(0.1, 0.55, inner.x)), fill(d2));
    over(acc, vec3(0.80, 0.90, 1.0), stroke(d2, 0.005));

    // Stamens around a green heart.
    float r = length(p);
    over(acc, vec3(0.05, 0.45, 0.20), fill(r - 0.12));
    vec2 stamen = foldPolar(rotate(p, scene.time * 0.2), 12.0);
    over(acc, GOLD, fill(length(stamen - vec2(0.14, 0.0)) - 0.022));
    return acc;
}

// White Tara: a moon disc holding an eye of wisdom that blinks now and then.
vec4 wisdomEye(vec2 p) {
    vec4 acc = vec4(0.0);
    float r = length(p);
    if (r > 1.1) return acc;

    seatDisc(acc, p, vec3(1.0), vec3(0.62, 0.68, 0.88), 0.92);

    // A blink closes the lids for a moment every seven seconds.
    float cycle = mod(scene.time, 7.0);
    float open = 1.0 - smoothstep(0.0, 0.08, cycle) * (1.0 - smoothstep(0.08, 0.2, cycle));

    float lids = sdPetal(p, 0.62, max(0.26 * open, 0.004));
    over(acc, DEEP_GOLD, stroke(lids, 0.03));
    over(acc, vec3(0.98, 0.97, 0.94), fill(lids));

    float iris = length(p) - 0.19;
    vec3 irisColor = mix(vec3(0.10, 0.28, 0.55), vec3(0.45, 0.72, 0.95), smoothstep(0.19, 0.03, length(p)));
    float inside = fill(lids);
    over(acc, irisColor, fill(iris) * inside);
    over(acc, vec3(0.02, 0.02, 0.06), fill(length(p) - 0.08) * inside);
    over(acc, vec3(1.0), fill(length(p - vec2(0.05, 0.06)) - 0.03) * inside);
    over(acc, GOLD, stroke(lids, 0.008));
    return acc;
}

// Vajrapani: an upright vajra blazing in front of a sun disc.
vec4 sunVajra(vec2 p) {
    vec4 acc = vec4(0.0);
    float r = length(p);
    if (r > 1.15) return acc;

    vec2 ray = foldPolar(rotate(p, scene.time * 0.1), 16.0);
    float rays = sdPetal(ray - vec2(0.88, 0.0), 0.2, 0.07);
    over(acc, mix(vec3(1.0, 0.45, 0.05), vec3(1.0, 0.85, 0.35), smoothstep(1.05, 0.7, ray.x)), fill(rays));

    seatDisc(acc, p, vec3(1.0, 0.55, 0.04), vec3(0.70, 0.10, 0.0), 0.78);

    // Blue flames licking up around the vajra.
    float n = fbm(p * 4.0 - vec2(0.0, scene.time * 1.8));
    float blaze = n * 1.3 - length(p * vec2(2.6, 0.9)) * 1.3 + 0.02;
    over(acc, mix(vec3(0.02, 0.10, 0.75), vec3(0.5, 0.8, 1.0), smoothstep(0.0, 0.5, blaze)),
         smoothstep(0.0, 0.08, blaze) * 0.7);

    const float SCALE = 3.6;
    float vajra = sdVajra(p.yx / SCALE) * SCALE;
    over(acc, vec3(0.0), 0.4 * smoothstep(0.08, -0.01, vajra));
    over(acc, mix(DEEP_GOLD, vec3(1.0, 0.86, 0.45), 0.35 + 0.65 * smoothstep(0.1, -0.1, p.x)), fill(vajra));
    over(acc, DEEP_GOLD * 0.6, stroke(vajra, 0.006));
    over(acc, BONE, fill(r - 0.045));
    return acc;
}

vec4 centre(vec2 p, int deity) {
    if (deity == DEITY_GREEN_TARA) return utpala(p);
    if (deity == DEITY_WHITE_TARA) return wisdomEye(p);
    if (deity == DEITY_VAJRAPANI)  return sunVajra(p);
    return dharmodaya(p);
}

vec4 glow(vec2 p, vec2 size) {
    float d = length(p) / (0.5 * min(size.x, size.y));
    float a = exp(-d * d * 5.0) + 0.6 * exp(-d * d * 40.0);
    a *= 1.0 - smoothstep(0.8, 1.0, d);
    return vec4(vec3(a), a);
}

vec4 vignette(vec2 p, vec2 size) {
    float d = length(p / (0.5 * size));
    float a = smoothstep(0.45, 1.3, d) * 0.9;
    return vec4(vec3(0.0), a);
}

vec4 lampCup(vec2 p) {
    vec4 acc = vec4(0.0);
    float r = length(p);
    if (r > 0.2) return acc;
    over(acc, vec3(0.0), 0.5 * smoothstep(0.19, 0.12, r));
    over(acc, mix(GOLD, DEEP_GOLD, smoothstep(0.02, 0.14, r)), fill(r - 0.13));
    over(acc, BONE, stroke(r - 0.13, 0.006));
    over(acc, vec3(0.35, 0.20, 0.05), fill(r - 0.075));
    return acc;
}

void main() {
    vec2 size = push.shape.yz;
    vec2 p = fragLocal * size;
    int layer = int(push.shape.x + 0.5);
    int deity = int(scene.deity + 0.5);
    Palette pal = paletteFor(deity);

    vec4 color;
    if      (layer == LAYER_SKY)      color = sky(p, pal);
    else if (layer == LAYER_FIRE)     color = fireRing(p, pal);
    else if (layer == LAYER_VAJRA)    color = vajraRing(p, pal);
    else if (layer == LAYER_PETALS)   color = petalRing(p, pal);
    else if (layer == LAYER_PALACE)   color = palace(p, pal);
    else if (layer == LAYER_LOTUS)    color = lotus(p, pal);
    else if (layer == LAYER_CENTRE)   color = centre(p, deity);
    else if (layer == LAYER_GLOW)     color = glow(p, size);
    else if (layer == LAYER_VIGNETTE) color = vignette(p, size);
    else if (layer == LAYER_LAMP)     color = lampCup(p);
    else                              color = vec4(1.0, 0.0, 1.0, 1.0);

    color.rgb *= push.tintColor.rgb;
    color *= push.tintColor.a;
    if (color.a < 0.002 && max(color.r, max(color.g, color.b)) < 0.002) {
        discard;
    }
    outColor = color;
}
