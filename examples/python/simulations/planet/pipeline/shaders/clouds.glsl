// The cloud layer: a shell between clouds.layer.x and .y metres above the
// sphere. Include after camera.glsl (which declares `clouds`, CloudParams
// in pipeline.json), terrain.glsl (for noise3) and atmosphere.glsl:
//     vec4 layer;      // bottom, top (m), coverage (0 none .. 1 overcast), extinction per metre at full density
//     vec4 shape;      // coverage noise frequency (per unit of direction), detail scale (m), detail erosion, 1 on / 0 off
//     vec4 wind;       // xyz: offset of the coverage noise, in its units (moves with the wind)
//
// Density at a point: the coverage, a noise on the direction from the
// planet's centre (the weather, the same at every height) raised so that
// the requested share of the sky is cloud, less a threshold that rises
// from the layer's bottom to its top, so that denser weather reaches
// higher: each cloud is a dome whose height follows its weather, on a flat
// base where the density fades in over the layer's lowest 6 %. A 3D detail
// noise erodes the edges. The noises' octaves are each turned by
// CLOUD_ROTATION, so their lattices do not line up along the axes.

const int CLOUD_STEPS = 64;                 // along the view ray through the shell, growing with distance
const float CLOUD_MAX_LENGTH = 50000.0;     // metres of the shell marched at most, from where the ray enters it
const int CLOUD_SUN_STEPS = 4;              // towards the sun, each twice the last
const float CLOUD_SUN_FIRST = 80.0;         // metres: the first of them (the four reach 1.2 km)
const int CLOUD_OCTAVES = 4;                // of multiple scattering
const float CLOUD_AMBIENT_SUN = 0.05;       // the sunlight scattered through the layer from everywhere, per sr

float cloudHeightFraction(vec3 p) {
    return (length(p) - atmosphere.radii.x - clouds.layer.x) / (clouds.layer.y - clouds.layer.x);
}

// A rotation about an axis off every coordinate axis.
const mat3 CLOUD_ROTATION = mat3(0.00, 0.80, 0.60,
                                 -0.80, 0.36, -0.48,
                                 -0.60, -0.48, 0.64);

// fBm of noise3 with each octave turned by CLOUD_ROTATION, filtered for a
// pixel `cell` lattice cells across at the first octave: an octave fades
// out as its cells shrink from 4 to 2 pixels across, before its features
// are single pixels. `slope` is about how
// fast the sum changes per lattice cell of the first octave, over the
// octaves kept.
float cloudFbm(vec3 p, int octaves, float lacunarity, float cell, out float slope) {
    float amplitude = 0.5, sum = 0.0, frequency = 1.0;
    slope = 0.0;
    for (int i = 0; i < octaves; ++i) {
        float keep = 1.0 - smoothstep(0.25, 0.5, cell);
        if (keep <= 0.0) break;
        sum += amplitude * keep * noise3(p);
        slope += amplitude * keep * frequency;
        p = CLOUD_ROTATION * p * lacunarity + vec3(19.1, 7.3, 3.7);
        cell *= lacunarity;
        frequency *= lacunarity;
        amplitude *= 0.5;
    }
    return sum;
}

const float CLOUD_EDGE_WIDEN_MAX = 0.2;   // of the coverage noise, the most its edges widen by

// The weather at the unit direction `dir`, for a pixel `footprint` metres
// across at the point: 0 clear, rising to 1 in the densest cloud; about
// clouds.layer.z of the sky is above 0. The rise is widened by how much the
// noise changes across the pixel (at most CLOUD_EDGE_WIDEN_MAX), so a
// cloud's edge is never sharper than a pixel.
float cloudCoverage(vec3 dir, float footprint) {
    float slope;
    float cell = footprint * clouds.shape.x / atmosphere.radii.x;
    float n = cloudFbm(CLOUD_ROTATION * dir * clouds.shape.x + clouds.wind.xyz, 4, 2.03, cell, slope) * 0.5 + 0.5;
    float c = clouds.layer.z;
    float widen = min(0.5 * slope * cell, CLOUD_EDGE_WIDEN_MAX);
    return smoothstep(1.0 - c - 0.05 - widen, 1.0 - c + 0.35 + widen, n);
}

// The coverage a point needs to be inside a cloud at the height fraction
// `f`: none up to 8 % of the layer, rising to all of it at the top.
float cloudThreshold(float f) {
    return smoothstep(0.08, 1.0, f);
}

// Density without the detail noise, for a pixel `footprint` metres across.
float cloudBaseDensity(vec3 p, float footprint) {
    float f = cloudHeightFraction(p);
    if (f <= 0.0 || f >= 1.0) return 0.0;
    float coverage = cloudCoverage(normalize(p), footprint);
    return clamp((coverage - cloudThreshold(f)) * 2.5, 0.0, 1.0) * smoothstep(0.0, 0.06, f);
}

// The same, unfiltered: for the light's march towards the sun and the shadows.
float cloudBaseDensity(vec3 p) {
    return cloudBaseDensity(p, 0.0);
}

float cloudDensity(vec3 p, float footprint) {
    float base = cloudBaseDensity(p, footprint);
    if (base <= 0.0) return 0.0;
    // The detail drifts with the coverage: the same wind, in metres.
    vec3 drift = clouds.wind.xyz * (atmosphere.radii.x / clouds.shape.x / clouds.shape.y);
    float slope;
    float detail = cloudFbm(p / clouds.shape.y + drift, 3, 2.3, footprint / clouds.shape.y, slope) * 0.5 + 0.5;
    return max(base - clouds.shape.z * detail * (1.0 - base), 0.0);
}

// The share of the sunlight that comes through the clouds to `p` (planet
// space): the base density at CLOUD_SHADOW_STEPS points along the sun's
// direction through the layer (from `p` where it is inside; at most
// CLOUD_SHADOW_LENGTH of it), as a transmittance, plus CLOUD_DIFFUSE of
// what they stop, which reaches the ground scattered through them. 1
// above the layer.
const int CLOUD_SHADOW_STEPS = 4;
const float CLOUD_SHADOW_LENGTH = 6000.0;
const float CLOUD_DIFFUSE = 0.3;

float cloudShadow(vec3 p) {
    if (clouds.shape.w == 0.0) return 1.0;
    vec3 sun = atmosphere.sun.xyz;
    float r = atmosphere.radii.x;
    vec2 top = raySphere(p, sun, r + clouds.layer.y);
    if (top.y <= 0.0) return 1.0;
    vec2 bottom = raySphere(p, sun, r + clouds.layer.x);
    float start = (bottom.x <= 0.0 && bottom.y > 0.0) ? bottom.y : 0.0;   // below the layer: from where it is entered
    float span = min(top.y - start, CLOUD_SHADOW_LENGTH);
    if (span <= 0.0) return 1.0;
    float density = 0.0;
    for (int k = 0; k < CLOUD_SHADOW_STEPS; ++k) {
        density += cloudBaseDensity(p + sun * (start + (float(k) + 0.5) / float(CLOUD_SHADOW_STEPS) * span));
    }
    float through = exp(-density / float(CLOUD_SHADOW_STEPS) * span * clouds.layer.w);
    return through + (1.0 - through) * CLOUD_DIFFUSE;
}

float henyeyGreenstein(float mu, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * ATMOSPHERE_PI * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

float cloudPhase(float mu, float scale) {
    return mix(henyeyGreenstein(mu, 0.6 * scale), henyeyGreenstein(mu, -0.25 * scale), 0.3);
}

// The (near, far) stretches along the unit ray `rd` from `ro` inside the
// shell, up to `tMax`: `a` the first, `b` the second (where the ray passes
// the inner sphere's far side), each with far <= near when empty.
void cloudShell(vec3 ro, vec3 rd, float tMax, out vec2 a, out vec2 b) {
    float r = atmosphere.radii.x;
    vec2 outer = raySphere(ro, rd, r + clouds.layer.y);
    vec2 inner = raySphere(ro, rd, r + clouds.layer.x);
    a = vec2(0.0);
    b = vec2(0.0);
    if (outer.y <= 0.0) return;
    float o0 = max(outer.x, 0.0), o1 = min(outer.y, tMax);
    if (inner.y <= 0.0) {                       // the ray misses the inner sphere, or it lies behind
        a = vec2(o0, o1);
    } else if (inner.x <= 0.0) {                // inside the inner sphere: out through the shell
        a = vec2(max(inner.y, 0.0), o1);
    } else {                                    // above it: down to it, and on beyond it
        a = vec2(o0, min(inner.x, o1));
        b = vec2(max(inner.y, o0), o1);
    }
}

// Light and transmittance of the clouds along the view ray: rgb the light
// they send towards the camera, a their transmittance; `distance` their
// mean distance, weighted by how much each step adds. The steps cover the
// shell's stretches in order, at most CLOUD_MAX_LENGTH of them (from orbit
// the shell is far, but the stretch through it short), spaced with the
// square of their index so
// they are short near the camera; `jitter` (0 .. 1) shifts them to turn
// banding into noise. The sunlight inside the clouds stands for light
// scattered many times by CLOUD_OCTAVES octaves, each with 0.3 times the
// extinction, half the phase function's anisotropy and half the weight of
// the last. Light from all around adds the sky's and CLOUD_AMBIENT_SUN of
// the sunlight above the clouds (scattered through the whole layer, it
// lights even their depths), both brighter towards the layer's top. The
// density is filtered for a pixel `pixelAngle` radians across.
vec4 marchClouds(vec3 ro, vec3 rd, float tMax, float jitter, float pixelAngle, out float distance) {
    distance = 0.0;
    vec2 a, b;
    cloudShell(ro, rd, tMax, a, b);
    float lenA = min(max(a.y - a.x, 0.0), CLOUD_MAX_LENGTH);
    float lenB = min(max(b.y - b.x, 0.0), CLOUD_MAX_LENGTH - lenA);
    float total = lenA + lenB;
    if (total <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 sun = atmosphere.sun.xyz;
    float mu = dot(rd, sun);
    float sigmaMax = clouds.layer.w;

    float transmittance = 1.0;
    vec3 light = vec3(0.0);
    float weight = 0.0;
    float previous = 0.0;
    for (int i = 0; i < CLOUD_STEPS; ++i) {
        float q = (float(i) + jitter) / float(CLOUD_STEPS);
        float s = total * q * q;
        float dt = s - previous;
        previous = s;
        float t = s < lenA ? a.x + s : b.x + (s - lenA);
        vec3 p = ro + rd * t;
        float density = cloudDensity(p, t * pixelAngle);
        if (density <= 0.0) continue;
        float sigma = density * sigmaMax;
        // The cloud between the point and the sun.
        float tau = 0.0;
        float ls = CLOUD_SUN_FIRST;
        float travelled = 0.0;
        for (int k = 0; k < CLOUD_SUN_STEPS; ++k) {
            tau += cloudBaseDensity(p + sun * (travelled + 0.5 * ls)) * sigmaMax * ls;
            travelled += ls;
            ls *= 2.0;
        }
        float scattered = 0.0, octaveWeight = 1.0, octaveTau = 1.0, octavePhase = 1.0;
        for (int o = 0; o < CLOUD_OCTAVES; ++o) {
            scattered += octaveWeight * exp(-tau * octaveTau) * cloudPhase(mu, octavePhase);
            octaveWeight *= 0.5;
            octaveTau *= 0.3;
            octavePhase *= 0.5;
        }
        vec3 radial = normalize(p);
        float height = cloudHeightFraction(p);
        vec3 sunlight = sunlightAt(p);
        vec3 lit = sunlight * scattered
                 + (skylightAt(p, radial) / ATMOSPHERE_PI + sunlight * CLOUD_AMBIENT_SUN) * (0.4 + 0.6 * height);
        float stepTransmittance = exp(-sigma * dt);
        float added = transmittance * (1.0 - stepTransmittance);
        light += lit * added;
        distance += t * added;
        weight += added;
        transmittance *= stepTransmittance;
        if (transmittance < 0.01) break;
    }
    distance = weight > 0.0 ? distance / weight : 0.0;
    return vec4(light, transmittance);
}
