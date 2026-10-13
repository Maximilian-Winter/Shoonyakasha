// optics.glsl - polarised light and the wave-optical materials.
//
// Polarisation is a Stokes vector (I, Q, U, V) in a frame given by an x axis
// perpendicular to the direction of travel, with Q = I_x - I_y; surfaces
// act on it through 4 x 4 Mueller matrices (Mueller-Stokes calculus). The
// Fresnel equations are the standard complex ones (Born & Wolf, sec. 1.5).
//
// Coherence follows Steinberg et al.: light reaching a surface from a
// source that subtends an angular radius a carries a transverse coherence
// width sigma (in micrometres) set so that the coherence blur of a
// specular lobe, lambda / (2 pi sigma) in direction cosines, spreads it as
// wide as the source appears, a / 2 (standard deviation); this is the
// relation |Theta| = lambda^2 / Omega of "A Generalized Ray Formulation For
// Wave-Optics Rendering" (Eq. 41 of the arXiv version), for the angular
// spread of a disc.

#ifndef OPTICS_GLSL
#define OPTICS_GLSL

const float PI = 3.14159265358979;

// ── Complex numbers (x real, y imaginary) ────────────────────────────────

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / dot(b, b); }
vec2 cconj(vec2 a) { return vec2(a.x, -a.y); }
float cabs2(vec2 a) { return dot(a, a); }
vec2 cexpi(float phase) { return vec2(cos(phase), sin(phase)); }
vec2 csqrt(vec2 z) {
    float r = length(z);
    float re = sqrt(max(0.0, 0.5 * (r + z.x)));
    float im = sqrt(max(0.0, 0.5 * (r - z.x)));
    return vec2(re, z.y < 0.0 ? -im : im);
}

// ── Fresnel ──────────────────────────────────────────────────────────────

struct Fresnel {
    vec2 rs, rp;      // reflection amplitudes, s and p
    vec2 ts, tp;      // transmission amplitudes
    vec2 cosT;        // cosine of the transmitted angle
};

// From a medium of index n1 into one of complex index n2, at the cosine of
// the incidence angle cosI.
Fresnel fresnel(vec2 n1, vec2 n2, float cosI) {
    vec2 sinI2 = vec2(1.0 - cosI * cosI, 0.0);
    vec2 ratio = cdiv(n1, n2);
    vec2 cosT = csqrt(vec2(1.0, 0.0) - cmul(cmul(ratio, ratio), sinI2));
    vec2 n1cosI = n1 * cosI, n2cosI = n2 * cosI;
    vec2 n1cosT = cmul(n1, cosT), n2cosT = cmul(n2, cosT);
    Fresnel f;
    f.rs = cdiv(n1cosI - n2cosT, n1cosI + n2cosT);
    f.rp = cdiv(n2cosI - n1cosT, n2cosI + n1cosT);
    f.ts = cdiv(2.0 * n1cosI, n1cosI + n2cosT);
    f.tp = cdiv(2.0 * n1cosI, n2cosI + n1cosT);
    f.cosT = cosT;
    return f;
}

// ── Mueller matrices (GLSL matrices are column-major: m[column][row]) ────

// The Mueller matrix of a reflection with amplitudes rs and rp, in the s-p frame.
mat4 muellerFromAmplitudes(vec2 rs, vec2 rp) {
    float a = 0.5 * (cabs2(rs) + cabs2(rp));
    float b = 0.5 * (cabs2(rs) - cabs2(rp));
    vec2 c = cmul(rs, cconj(rp));
    return mat4(a, b, 0.0, 0.0,
                b, a, 0.0, 0.0,
                0.0, 0.0, c.x, -c.y,
                0.0, 0.0, c.y, c.x);
}

// The Stokes vector in frame `to` of light in frame `from`, both x axes
// perpendicular to the direction of travel.
mat4 stokesRotation(vec3 from, vec3 to, vec3 direction) {
    float phi = atan(dot(cross(from, to), direction), dot(from, to));
    float c = cos(2.0 * phi), s = sin(2.0 * phi);
    return mat4(1.0, 0.0, 0.0, 0.0,
                0.0, c, -s, 0.0,
                0.0, s, c, 0.0,
                0.0, 0.0, 0.0, 1.0);
}

// The first row of an ideal linear polariser at `angle` to the frame's x axis.
vec4 polariserRow(float angle) {
    return 0.5 * vec4(1.0, cos(2.0 * angle), sin(2.0 * angle), 0.0);
}

// The s axis of a reflection from direction wi to wo about half vector h:
// perpendicular to the plane of the two directions.
vec3 sAxis(vec3 wi, vec3 h, vec3 fallback) {
    vec3 s = cross(h, wi);
    float len = length(s);
    return len > 1e-6 ? s / len : fallback;
}

// ── Coherence ────────────────────────────────────────────────────────────

// The transverse coherence width (micrometres) of light at wavelength
// lambdaUm from a source of angular radius a (radians).
float coherenceWidth(float lambdaUm, float a) {
    return lambdaUm / (PI * max(a, 1e-5));
}

// ── Bessel functions of the first kind ───────────────────────────────────

// J_n(x) for n >= 0, by its power series (x below about 8).
float besselJ(int n, float x) {
    float hx = 0.5 * x;
    float term = 1.0;
    for (int i = 1; i <= n; ++i) term *= hx / float(i);
    float sum = term;
    float h2 = hx * hx;
    for (int m = 1; m < 30; ++m) {
        term *= -h2 / (float(m) * float(m + n));
        sum += term;
        if (abs(term) < 1e-7 * abs(sum)) break;
    }
    return sum;
}

#endif
