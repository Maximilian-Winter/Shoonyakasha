// The planet's atmosphere: single scattering of sunlight by air (Rayleigh)
// and haze (Mie), both thinning exponentially with height, and the sunlight
// that reaches a point through it.
//
// Include after declaring a uniform block named `atmosphere` with the
// fields of AtmosphereParams in pipeline.json:
//     vec4 radii;      // planet radius, top of the atmosphere, Rayleigh and Mie scale heights (m)
//     vec4 rayleigh;   // rgb: Rayleigh scattering per metre at height 0; w: Mie asymmetry g
//     vec4 mie;        // Mie scattering and absorption per metre at height 0, sun illuminance, exposure
//     vec4 sun;        // xyz: unit direction towards the sun
// Positions are relative to the planet's centre, in metres.

const float ATMOSPHERE_PI = 3.14159265358979;
const int VIEW_STEPS = 32;
const int SUN_STEPS = 8;

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

// Rayleigh and Mie densities, 1 at height 0.
vec2 airDensity(vec3 p) {
    float h = length(p) - atmosphere.radii.x;
    return exp(-h / atmosphere.radii.zw);
}

vec3 extinction(vec2 density) {
    return atmosphere.rayleigh.rgb * density.x + (atmosphere.mie.x + atmosphere.mie.y) * density.y;
}

// Optical depth from `p` to the top of the atmosphere towards the sun.
vec3 opticalDepthToSun(vec3 p) {
    float far = raySphere(p, atmosphere.sun.xyz, atmosphere.radii.y).y;
    float dt = max(far, 0.0) / float(SUN_STEPS);
    vec3 depth = vec3(0.0);
    for (int i = 0; i < SUN_STEPS; ++i) {
        depth += extinction(airDensity(p + atmosphere.sun.xyz * (dt * (float(i) + 0.5)))) * dt;
    }
    return depth;
}

bool inPlanetShadow(vec3 p) {
    vec2 hit = raySphere(p, atmosphere.sun.xyz, atmosphere.radii.x);
    return hit.y > 0.0 && hit.x > 0.0;
}

// The sunlight (illuminance per colour) arriving at `p`.
vec3 sunlightAt(vec3 p) {
    if (inPlanetShadow(p)) return vec3(0.0);
    return atmosphere.mie.z * exp(-opticalDepthToSun(p));
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

float rayleighPhase(float mu) {
    return 3.0 / (16.0 * ATMOSPHERE_PI) * (1.0 + mu * mu);
}

float miePhase(float mu, float g) {
    float g2 = g * g;
    return 3.0 / (8.0 * ATMOSPHERE_PI) * ((1.0 - g2) * (1.0 + mu * mu)) /
           ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

// Light scattered towards `ro` along the unit ray `rd` up to distance
// `tMax`, and the transmittance over that stretch: what behind it is seen
// through. Steps are denser near where the ray enters the atmosphere.
void atmosphereAlong(vec3 ro, vec3 rd, float tMax, out vec3 inscatter, out vec3 transmittance) {
    inscatter = vec3(0.0);
    transmittance = vec3(1.0);
    vec2 shell = raySphere(ro, rd, atmosphere.radii.y);
    if (shell.y < 0.0) return;
    float t0 = max(shell.x, 0.0);
    float t1 = min(shell.y, tMax);
    if (t1 <= t0) return;

    vec3 depth = vec3(0.0);
    vec3 rayleighSum = vec3(0.0), mieSum = vec3(0.0);
    for (int i = 0; i < VIEW_STEPS; ++i) {
        float s0 = float(i) / float(VIEW_STEPS), s1 = float(i + 1) / float(VIEW_STEPS);
        float ta = t0 + (t1 - t0) * s0 * s0;
        float tb = t0 + (t1 - t0) * s1 * s1;
        float dt = tb - ta;
        vec3 p = ro + rd * (0.5 * (ta + tb));
        vec2 density = airDensity(p) * dt;
        vec3 segment = atmosphere.rayleigh.rgb * density.x + (atmosphere.mie.x + atmosphere.mie.y) * density.y;
        vec3 toHere = depth + 0.5 * segment;
        depth += segment;
        if (inPlanetShadow(p)) continue;
        vec3 reaching = exp(-(toHere + opticalDepthToSun(p)));
        rayleighSum += reaching * density.x;
        mieSum += reaching * density.y;
    }
    float mu = dot(rd, atmosphere.sun.xyz);
    inscatter = atmosphere.mie.z * (rayleighSum * atmosphere.rayleigh.rgb * rayleighPhase(mu) +
                                    mieSum * atmosphere.mie.x * miePhase(mu, atmosphere.rayleigh.w));
    transmittance = exp(-depth);
}
