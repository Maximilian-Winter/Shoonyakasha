// The planet's atmosphere: scattering of sunlight by air (Rayleigh) and
// haze (Mie), both thinning exponentially with height, absorption by haze
// and by an ozone layer (a tent around a peak height), and the sunlight
// that reaches a point through it.
//
// Include after declaring a uniform block named `atmosphere` with the
// fields of AtmosphereParams in pipeline.json:
//     vec4 radii;      // planet radius, top of the atmosphere, Rayleigh and Mie scale heights (m)
//     vec4 rayleigh;   // rgb: Rayleigh scattering per metre at height 0; w: Mie asymmetry g
//     vec4 mie;        // Mie scattering and absorption per metre at height 0, sun illuminance, exposure
//     vec4 sun;        // xyz: unit direction towards the sun
//     vec4 ozone;      // rgb: ozone absorption per metre at its peak; w: the peak's height (m)
//     vec4 settings;   // x: the ozone layer's half width (m); y: multiple scattering's weight (0 or 1)
// Positions are relative to the planet's centre, in metres.
//
// Two lookup tables (Hillaire 2020) hold what depends on height and the
// sun's angle alone. Define ATMOSPHERE_TRANSMITTANCE_LUT and declare
// `transmittanceLut` for sunTransmittance and what uses it; define
// ATMOSPHERE_MULTI_SCATTERING_LUT and declare `multiScatteringLut` as well
// for atmosphereAlong.
//   - Transmittance (TRANSMITTANCE_SIZE): from a point at radius r towards
//     the top along a direction at cosine mu from the zenith, for rays that
//     do not meet the planet, mapped as transmittanceLutUv lays out.
//   - Multiple scattering (MULTI_SCATTERING_SIZE): the light scattered
//     more than once that reaches a point, per unit of scattering, by
//     height (v) and the cosine of the sun's zenith angle (u, from -1 to 1).

const float ATMOSPHERE_PI = 3.14159265358979;
const int VIEW_STEPS = 32;
const ivec2 TRANSMITTANCE_SIZE = ivec2(256, 64);
const ivec2 MULTI_SCATTERING_SIZE = ivec2(32, 32);

// (near, far) distances along the unit ray `rd` from `ro` where it meets
// the sphere of radius `r` about the centre; far < 0 when it misses. Works
// from the ray's closest point to the centre, which keeps its precision
// with `ro` thousands of kilometres out, where |ro|^2 - r^2 would not.
vec2 raySphere(vec3 ro, vec3 rd, float r) {
    float b = dot(ro, rd);
    vec3 closest = ro - b * rd;
    float h = r * r - dot(closest, closest);
    if (h < 0.0) return vec2(-1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

// Rayleigh, Mie and ozone densities at height `h` (m), 1 at their peak.
vec3 airDensityAt(float h) {
    vec2 exponential = exp(-max(h, -2000.0) / atmosphere.radii.zw);
    float ozone = max(0.0, 1.0 - abs(h - atmosphere.ozone.w) / atmosphere.settings.x);
    return vec3(exponential, ozone);
}

vec3 airDensity(vec3 p) {
    return airDensityAt(length(p) - atmosphere.radii.x);
}

vec3 extinction(vec3 density) {
    return atmosphere.rayleigh.rgb * density.x + (atmosphere.mie.x + atmosphere.mie.y) * density.y
         + atmosphere.ozone.rgb * density.z;
}

bool inPlanetShadow(vec3 p) {
    vec2 hit = raySphere(p, atmosphere.sun.xyz, atmosphere.radii.x);
    return hit.y > 0.0 && hit.x > 0.0;
}

float rayleighPhase(float mu) {
    return 3.0 / (16.0 * ATMOSPHERE_PI) * (1.0 + mu * mu);
}

float miePhase(float mu, float g) {
    float g2 = g * g;
    return 3.0 / (8.0 * ATMOSPHERE_PI) * ((1.0 - g2) * (1.0 + mu * mu)) /
           ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

// ── The transmittance table's layout ──
// H is the distance from the ground to the top along the horizon at the
// ground; rho the same from radius r. v = rho / H; u places the distance
// to the top along mu between its least (straight up) and greatest (along
// the horizon) from r.

vec2 transmittanceLutUv(float r, float mu) {
    float bottom = atmosphere.radii.x, top = atmosphere.radii.y;
    float H = sqrt(top * top - bottom * bottom);
    float rho = sqrt(max(r * r - bottom * bottom, 0.0));
    float d = max(0.0, -r * mu + sqrt(max(r * r * (mu * mu - 1.0) + top * top, 0.0)));
    float dMin = top - r, dMax = rho + H;
    float u = (d - dMin) / max(dMax - dMin, 1e-3);
    float v = rho / H;
    // Texel centres: the table's first and last texels sit at 0 and 1.
    vec2 size = vec2(TRANSMITTANCE_SIZE);
    return (clamp(vec2(u, v), 0.0, 1.0) * (size - 1.0) + 0.5) / size;
}

// (r, mu) of a texel's (u, v) in 0 .. 1, as transmittanceLutUv lays them out.
vec2 transmittanceLutRMu(vec2 uv) {
    float bottom = atmosphere.radii.x, top = atmosphere.radii.y;
    float H = sqrt(top * top - bottom * bottom);
    float rho = H * uv.y;
    float r = sqrt(rho * rho + bottom * bottom);
    float dMin = top - r, dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);
    float mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    return vec2(r, clamp(mu, -1.0, 1.0));
}

vec2 multiScatteringLutUv(float r, float sunMu) {
    float v = (r - atmosphere.radii.x) / (atmosphere.radii.y - atmosphere.radii.x);
    vec2 size = vec2(MULTI_SCATTERING_SIZE);
    return (clamp(vec2(sunMu * 0.5 + 0.5, v), 0.0, 1.0) * (size - 1.0) + 0.5) / size;
}

#ifdef ATMOSPHERE_TRANSMITTANCE_LUT
// Transmittance from `p` to space towards the sun: 0 in the planet's
// shadow, 1 above the atmosphere.
vec3 sunTransmittance(vec3 p) {
    if (inPlanetShadow(p)) return vec3(0.0);
    float r = length(p);
    if (r >= atmosphere.radii.y) return vec3(1.0);
    float mu = dot(p, atmosphere.sun.xyz) / r;
    return texture(transmittanceLut, transmittanceLutUv(max(r, atmosphere.radii.x), mu)).rgb;
}

// The sunlight (illuminance per colour) arriving at `p`.
vec3 sunlightAt(vec3 p) {
    return atmosphere.mie.z * sunTransmittance(p);
}

// A rough sky light on a surface at `p` whose up is `radial`: blue, fading
// as the sun goes below the horizon.
vec3 skylightAt(vec3 p, vec3 radial) {
    float day = smoothstep(-0.15, 0.25, dot(radial, atmosphere.sun.xyz));
    return atmosphere.mie.z * vec3(0.025, 0.045, 0.08) * day;
}

// The radiance of a diffuse (Lambert) surface at `p` (planet space) with
// `albedo`, facing `normal`, where up is `radial`.
vec3 lightSurface(vec3 albedo, vec3 p, vec3 normal, vec3 radial) {
    float sunCos = max(dot(normal, atmosphere.sun.xyz), 0.0);
    float skyCos = 0.5 + 0.5 * dot(normal, radial);
    return albedo / ATMOSPHERE_PI * (sunlightAt(p) * sunCos + skylightAt(p, radial) * skyCos);
}
#endif

#ifdef ATMOSPHERE_MULTI_SCATTERING_LUT
// Light scattered towards `ro` along the unit ray `rd` up to distance
// `tMax`, and the transmittance over that stretch: what behind it is seen
// through. Each step adds the sunlight scattered once (with the phase
// functions) and, weighted by atmosphere.settings.y, the light scattered
// more than once from the multiple scattering table. Steps are denser
// near where the ray enters the atmosphere.
void atmosphereAlong(vec3 ro, vec3 rd, float tMax, out vec3 inscatter, out vec3 transmittance) {
    inscatter = vec3(0.0);
    transmittance = vec3(1.0);
    vec2 shell = raySphere(ro, rd, atmosphere.radii.y);
    if (shell.y < 0.0) return;
    float t0 = max(shell.x, 0.0);
    float t1 = min(shell.y, tMax);
    if (t1 <= t0) return;

    vec3 depth = vec3(0.0);
    vec3 rayleighSum = vec3(0.0), mieSum = vec3(0.0), multipleSum = vec3(0.0);
    for (int i = 0; i < VIEW_STEPS; ++i) {
        float s0 = float(i) / float(VIEW_STEPS), s1 = float(i + 1) / float(VIEW_STEPS);
        float ta = t0 + (t1 - t0) * s0 * s0;
        float tb = t0 + (t1 - t0) * s1 * s1;
        float dt = tb - ta;
        vec3 p = ro + rd * (0.5 * (ta + tb));
        float r = length(p);
        vec3 density = airDensityAt(r - atmosphere.radii.x) * dt;
        vec3 segment = extinction(density);
        vec3 toHere = exp(-(depth + 0.5 * segment));
        depth += segment;
        vec3 sun = sunTransmittance(p);
        rayleighSum += toHere * sun * density.x;
        mieSum += toHere * sun * density.y;
        vec3 scattering = atmosphere.rayleigh.rgb * density.x + atmosphere.mie.x * density.y;
        vec3 multiple = texture(multiScatteringLut, multiScatteringLutUv(r, dot(p, atmosphere.sun.xyz) / r)).rgb;
        multipleSum += toHere * scattering * multiple;
    }
    float mu = dot(rd, atmosphere.sun.xyz);
    inscatter = atmosphere.mie.z * (rayleighSum * atmosphere.rayleigh.rgb * rayleighPhase(mu)
                                    + mieSum * atmosphere.mie.x * miePhase(mu, atmosphere.rayleigh.w)
                                    + multipleSum * atmosphere.settings.y);
    transmittance = exp(-depth);
}
#endif
