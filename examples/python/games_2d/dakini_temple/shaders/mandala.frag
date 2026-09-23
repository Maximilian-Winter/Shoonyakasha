#version 450
//
// Procedural mandala layers. Every entity drawn with this shader is one quad;
// shape.x picks which layer is painted on it and shape.yz is the quad size in
// world units, so all layers share world-unit coordinates centred on their
// quad and line up when stacked. Edges are antialiased from signed distances.
//
// Output is premultiplied alpha; temple_pipeline.json blends it with
// (one, one_minus_src_alpha) for solid layers and (one, one) for glows.
//

layout(set = 0, binding = 0) uniform TempleSceneUBO {
    mat4 viewProjection;
    vec2 resolution;
    float time;
    float padding;
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 tintColor;
    vec4 shape;        // x = layer id, yz = quad size in world units, w = per-entity seed
} push;

layout(location = 0) in vec2 fragLocal;

layout(location = 0) out vec4 outColor;

const float TAU = 6.28318531;

const int LAYER_SKY        = 0;
const int LAYER_FIRE       = 1;
const int LAYER_VAJRA      = 2;
const int LAYER_PETALS     = 3;
const int LAYER_PALACE     = 4;
const int LAYER_LOTUS      = 5;
const int LAYER_DHARMODAYA = 6;
const int LAYER_GLOW       = 7;
const int LAYER_VIGNETTE   = 8;
const int LAYER_LAMP       = 9;

const vec3 GOLD      = vec3(1.00, 0.74, 0.28);
const vec3 DEEP_GOLD = vec3(0.55, 0.33, 0.08);
const vec3 BONE      = vec3(1.00, 0.94, 0.84);

// Colours of the four quarters, one per direction.
const vec3 EAST_BLUE    = vec3(0.10, 0.20, 0.62);
const vec3 SOUTH_YELLOW = vec3(0.86, 0.60, 0.08);
const vec3 WEST_RED     = vec3(0.68, 0.06, 0.07);
const vec3 NORTH_GREEN  = vec3(0.05, 0.44, 0.24);

// ── Noise ───────────────────────────────────────────────────────

// Integer hash of the lattice cell containing p, in [0, 1].
float hash(vec2 p) {
    uvec2 q = uvec2(ivec2(floor(p)));
    uint h = (q.x * 1597334677u) ^ (q.y * 3812015801u);
    h = h * 747796405u + 2891336453u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return float(h) * (1.0 / 4294967295.0);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        v += a * noise(p);
        p = p * 2.03 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

// ── Coverage and compositing ────────────────────────────────────

// Coverage of the region d < 0, antialiased over one pixel.
float fill(float d) {
    return clamp(0.5 - d / max(fwidth(d), 1e-5), 0.0, 1.0);
}

// Coverage of a line of half-width w along d = 0.
float stroke(float d, float w) {
    return fill(abs(d) - w);
}

// Premultiplied "over": paints colour at the given coverage on top of acc.
void over(inout vec4 acc, vec3 color, float alpha) {
    acc = vec4(color * alpha, alpha) + acc * (1.0 - alpha);
}

// ── Signed distance functions ───────────────────────────────────

float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Equilateral triangle pointing up (+y), centred on its centroid; r is half the side.
float sdTriangle(vec2 p, float r) {
    const float k = sqrt(3.0);
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}

// Vertical vesica: intersection of two circles of radius r whose centres are d apart from the axis.
float sdVesica(vec2 p, float r, float d) {
    p = abs(p);
    float b = sqrt(r * r - d * d);
    return ((p.y - b) * d > p.x * b) ? length(p - vec2(0.0, b)) : length(p - vec2(-d, 0.0)) - r;
}

// Pointed petal lying along x, centred on the origin.
float sdPetal(vec2 p, float halfLength, float halfWidth) {
    float r = 0.5 * (halfLength * halfLength / halfWidth + halfWidth);
    return sdVesica(p.yx, r, r - halfWidth);
}

float sdEllipse(vec2 p, vec2 radii) {
    return (length(p / radii) - 1.0) * min(radii.x, radii.y);
}

// Folds the plane into one of n equal angular sectors, centred on +x.
vec2 foldPolar(vec2 p, float n) {
    float sector = TAU / n;
    float a = mod(atan(p.y, p.x) + 0.5 * sector, sector) - 0.5 * sector;
    return length(p) * vec2(cos(a), sin(a));
}

vec2 rotate(vec2 p, float a) {
    float c = cos(a);
    float s = sin(a);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// ── Layers ──────────────────────────────────────────────────────

vec4 sky(vec2 p) {
    float r = length(p);
    vec3 col = mix(vec3(0.09, 0.02, 0.08), vec3(0.008, 0.006, 0.02), smoothstep(0.0, 9.0, r));

    float drift = scene.time * 0.015;
    col += vec3(0.30, 0.03, 0.10) * pow(fbm(p * 0.30 + vec2(drift, 0.0)), 3.0);
    col += vec3(0.04, 0.05, 0.22) * pow(fbm(p * 0.45 - vec2(3.1, drift)), 3.5);

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

vec4 fireRing(vec2 p) {
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
    vec3 flame = mix(vec3(0.45, 0.02, 0.02), vec3(0.95, 0.24, 0.03), smoothstep(0.0, 0.45, t));
    flame = mix(flame, vec3(1.0, 0.72, 0.30), smoothstep(0.6, 1.0, t));
    float flameAlpha = smoothstep(0.0, 0.10, heat) * fill(3.9 - r);
    over(acc, flame, flameAlpha);

    // Base of the fire: a solid band with a gold inner edge.
    over(acc, vec3(0.50, 0.04, 0.03), fill(abs(r - 3.93) - 0.06));
    over(acc, GOLD, stroke(r - 3.87, 0.012));
    return acc;
}

vec4 vajraRing(vec2 p) {
    float r = length(p);
    vec4 acc = vec4(0.0);
    if (r < 3.4 || r > 3.95) return acc;

    over(acc, vec3(0.03, 0.06, 0.20), fill(abs(r - 3.665) - 0.195));
    over(acc, GOLD, stroke(r - 3.47, 0.012));
    over(acc, GOLD, stroke(r - 3.86, 0.012));

    // Vajras laid end to end around the ring, x along the ring, y across it.
    const float COUNT = 40.0;
    vec2 f = foldPolar(p, COUNT);
    vec2 q = vec2(atan(f.y, f.x) * r, r - 3.665);
    vec2 m = vec2(abs(q.x), q.y);
    float hub = length(q) - 0.045;
    float bulb = sdEllipse(m - vec2(0.105, 0.0), vec2(0.06, 0.055));
    float centreProng = sdEllipse(m - vec2(0.195, 0.0), vec2(0.055, 0.016));
    float sideProngs = sdEllipse(vec2(m.x, abs(m.y)) - vec2(0.18, 0.052), vec2(0.05, 0.014));
    float vajra = min(min(hub, bulb), min(centreProng, sideProngs));

    over(acc, mix(DEEP_GOLD, GOLD, 0.35 + 0.65 * smoothstep(-0.06, 0.06, q.y)), fill(vajra));
    over(acc, DEEP_GOLD * 0.6, stroke(vajra, 0.004));
    over(acc, BONE, fill(length(q) - 0.016));
    return acc;
}

vec4 petalRing(vec2 p) {
    float r = length(p);
    vec4 acc = vec4(0.0);
    if (r < 2.95 || r > 3.55) return acc;

    over(acc, vec3(0.16, 0.01, 0.05), fill(abs(r - 3.25) - 0.23));

    const float COUNT = 32.0;
    float sector = TAU / COUNT;
    float index = floor((atan(p.y, p.x) + 0.5 * sector) / sector);
    vec2 q = foldPolar(p, COUNT);
    float d = sdPetal(q - vec2(3.26, 0.0), 0.19, 0.085);

    vec3 base = mod(index, 2.0) < 0.5 ? vec3(0.92, 0.28, 0.40) : vec3(0.98, 0.52, 0.30);
    vec3 col = mix(base * 0.55, mix(base, BONE, 0.35), smoothstep(3.08, 3.42, q.x));
    over(acc, col, fill(d));
    over(acc, vec3(0.35, 0.02, 0.08), stroke(d, 0.005));
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

vec4 palace(vec2 p) {
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
    over(acc, vec3(0.05, 0.02, 0.07), fill(r - 1.86));
    over(acc, GOLD, stroke(r - 1.86, 0.012));
    over(acc, GOLD, stroke(r - 1.74, 0.006));
    vec2 pearl = foldPolar(p, 48.0);
    over(acc, BONE, fill(length(pearl - vec2(1.80, 0.0)) - 0.03));
    return acc;
}

vec4 lotus(vec2 p) {
    vec4 acc = vec4(0.0);
    if (length(p) > 1.75) return acc;

    // Back row, turned half a sector so it shows between the front petals.
    vec2 back = foldPolar(rotate(p, TAU / 16.0), 8.0);
    float db = sdPetal(back - vec2(1.07, 0.0), 0.62, 0.33);
    over(acc, mix(vec3(0.12, 0.0, 0.02), vec3(0.50, 0.03, 0.07), smoothstep(0.5, 1.6, back.x)), fill(db));
    over(acc, vec3(0.95, 0.45, 0.40), stroke(db, 0.006));

    vec2 front = foldPolar(p, 8.0);
    float d = sdPetal(front - vec2(0.96, 0.0), 0.62, 0.36);
    vec3 col = mix(vec3(0.32, 0.004, 0.02), vec3(0.95, 0.26, 0.24), smoothstep(0.45, 1.6, front.x));
    col = mix(col, BONE, 0.35 * stroke(front.y, 0.008) * smoothstep(1.5, 0.6, front.x));
    over(acc, col, fill(d));
    over(acc, vec3(1.0, 0.80, 0.78), stroke(d, 0.008));

    over(acc, GOLD, fill(length(p) - 0.46));
    return acc;
}

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

    vec4 color;
    if      (layer == LAYER_SKY)        color = sky(p);
    else if (layer == LAYER_FIRE)       color = fireRing(p);
    else if (layer == LAYER_VAJRA)      color = vajraRing(p);
    else if (layer == LAYER_PETALS)     color = petalRing(p);
    else if (layer == LAYER_PALACE)     color = palace(p);
    else if (layer == LAYER_LOTUS)      color = lotus(p);
    else if (layer == LAYER_DHARMODAYA) color = dharmodaya(p);
    else if (layer == LAYER_GLOW)       color = glow(p, size);
    else if (layer == LAYER_VIGNETTE)   color = vignette(p, size);
    else if (layer == LAYER_LAMP)       color = lampCup(p);
    else                                color = vec4(1.0, 0.0, 1.0, 1.0);

    color.rgb *= push.tintColor.rgb;
    color *= push.tintColor.a;
    if (color.a < 0.002 && max(color.r, max(color.g, color.b)) < 0.002) {
        discard;
    }
    outColor = color;
}
