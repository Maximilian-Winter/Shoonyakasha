// fog.glsl - the stage's smoke and haze, shared by fog_scatter.comp,
// fog_integrate.comp, fog_apply.frag and lasers.frag.
//
// The fog lives in a grid that follows the camera: fogScatter and
// fogIntegrated are 2D-array images whose width and height divide the view
// and whose layers are slices of depth, spaced exponentially from
// fog.grid.x to fog.grid.y metres. Layer k covers view depths
// sliceDepth(k) to sliceDepth(k + 1).

#ifndef STAGE_FOG_GLSL
#define STAGE_FOG_GLSL

// scene.custom.fog.* (pipeline.json "Fog"); see README.md.
#define FOG_BLOCK                                                                 \
    vec4 grid;      /* x first depth, y last depth (metres), z history kept, w on */ \
    vec4 media;     /* x haze density, y smoke density, z ground fog density, w phase g */ \
    vec4 motion;    /* x seconds, y light scattered, z ground fog height, w noise scale */ \
    vec4 wind;      /* xyz drift (m/s) */                                         \
    vec4 hall;      /* x half width, y height, z half depth of the room the fog fills */ \
    vec4 machine0;  /* xyz nozzle, w output (0 off) */                            \
    vec4 machine1;                                                                \
    vec4 machine2;                                                                \
    vec4 machine3;                                                                \
    vec4 machineDir0; /* xyz direction the jet leaves in, w its speed (m/s) */   \
    vec4 machineDir1;                                                             \
    vec4 machineDir2;                                                             \
    vec4 machineDir3;                                                             \
    vec4 burst;     /* x when the machines last burst (seconds), y how much more they give, z how fast it fades */

// View depth at the start of slice t (continuous) of `slices`.
float sliceDepth(float t, float slices, vec4 grid) {
    return grid.x * pow(grid.y / grid.x, t / slices);
}

// The (continuous) slice at view depth d.
float depthSlice(float d, float slices, vec4 grid) {
    return log(max(d, 1e-4) / grid.x) / log(grid.y / grid.x) * slices;
}

// fogIntegrated at uv, after `t` slices (continuous) of `slices`: layer k
// holds the fog up to the end of slice k, so between layers the value is
// interpolated, and before the first it is none (rgb 0, transmittance 1).
vec4 fogAt(sampler2DArray integrated, vec2 uv, float t, float slices) {
    float k = clamp(t - 1.0, -1.0, slices - 1.0);
    float k0 = floor(k);
    vec4 a = k0 < 0.0 ? vec4(0.0, 0.0, 0.0, 1.0) : textureLod(integrated, vec3(uv, k0), 0.0);
    vec4 b = textureLod(integrated, vec3(uv, min(k0 + 1.0, slices - 1.0)), 0.0);
    return mix(a, b, k - k0);
}

// ── Noise ───────────────────────────────────────────────────────

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

// Value noise in 0..1, smooth across cells.
float valueNoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(mix(hash31(i), hash31(i + vec3(1, 0, 0)), u.x),
                      mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), u.x), u.y),
                  mix(mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), u.x),
                      mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1, 1, 1)), u.x), u.y), u.z);
    return a;
}

// Three octaves, 0..1 with mean about 0.5.
float fbm(vec3 p) {
    return 0.55 * valueNoise(p) + 0.3 * valueNoise(p * 2.13 + 7.1) + 0.15 * valueNoise(p * 4.37 + 3.7);
}

// ── Density ─────────────────────────────────────────────────────

// One smoke machine's plume at p: a jet from `nozzle` along `dir` that
// widens, slows and rises as it goes, carrying puffs of noise. A burst
// (burst.x seconds, burst.y more, fading at burst.z) travels out along it
// with the smoke that left the nozzle then.
float plume(vec3 p, vec4 nozzle, vec4 dir, float time, float noiseScale, vec4 burst) {
    if (nozzle.w <= 0.0) return 0.0;
    vec3 d = p - nozzle.xyz;
    float s = dot(d, dir.xyz);                  // metres along the jet
    if (s < -0.2) return 0.0;
    s = max(s, 0.0);
    // The jet rises as it slows: its axis curves up with distance.
    vec3 axis = nozzle.xyz + dir.xyz * s + vec3(0.0, 0.035 * s * s, 0.0);
    float width = 0.12 + 0.32 * s;
    float r2 = dot(p - axis, p - axis) / (width * width);
    if (r2 > 9.0) return 0.0;
    // Smoke leaving the nozzle at `speed` slowing over a few metres: how
    // long ago this part left, for the puffs and the noise it carries.
    float speed = max(dir.w, 0.1);
    float age = s / speed * (1.0 + 0.25 * s);
    float left = time - age;                     // when this smoke left the nozzle
    float puffs = 0.55 + 0.45 * sin(6.2831853 * left * 0.35 + nozzle.x * 1.7);
    float since = left - burst.x;
    puffs *= 1.0 + (since >= 0.0 ? burst.y * exp(-since * burst.z) : 0.0);
    vec3 carried = (p - dir.xyz * speed * time * 0.6) * noiseScale * 1.6;
    float n = fbm(carried + vec3(0.0, -time * 0.2, 0.0));
    float core = exp(-r2) * nozzle.w / (1.0 + 0.6 * s * s);
    return core * puffs * smoothstep(0.25, 0.75, n) * 2.0;
}

// Haze, ground fog and the machines' plumes at p (world space), per metre.
float fogDensity(vec3 p, vec4 media, vec4 motion, vec4 wind, vec4 hall,
                 vec4 m0, vec4 m1, vec4 m2, vec4 m3, vec4 d0, vec4 d1, vec4 d2, vec4 d3, vec4 burst) {
    // Inside the hall only, fading out over its last metre.
    vec3 inside = vec3(hall.x, hall.y, hall.z) - vec3(abs(p.x), max(p.y - 0.0, 0.0), abs(p.z));
    float walls = clamp(min(min(inside.x, inside.z), inside.y), 0.0, 1.0);
    if (walls <= 0.0 || p.y < 0.0) return 0.0;

    float time = motion.x;
    float noiseScale = motion.w;
    vec3 drifted = p - wind.xyz * time;
    float haze = media.x * (0.45 + 1.1 * fbm(drifted * noiseScale * 0.35));
    float ground = media.z * exp(-p.y / max(motion.z, 0.05))
                 * (0.3 + 1.4 * fbm(drifted * noiseScale * 0.9 + vec3(0.0, time * 0.05, 0.0)));
    float smoke = media.y * (plume(p, m0, d0, time, noiseScale, burst) + plume(p, m1, d1, time, noiseScale, burst)
                           + plume(p, m2, d2, time, noiseScale, burst) + plume(p, m3, d3, time, noiseScale, burst));
    return (haze + ground + smoke) * walls;
}

// Henyey-Greenstein phase function: the share of the light scattered into
// one direction, for cosTheta = dot(the direction the light travels, the
// direction from the point towards the eye). g > 0 scatters mostly forward.
float phaseHG(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

#endif
