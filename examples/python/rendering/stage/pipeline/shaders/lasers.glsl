// lasers.glsl - the stage's laser projectors, shared by laser_trace.comp
// and lasers.frag. The including shader declares the Lasers block as
// `lasers` (with LASERS_BLOCK from lasers_block.glsl) before including this.
//
// Each of LASER_PROJECTORS projectors has a pattern: 0 off, 1 a fan of
// beams that tilts and rolls, 2 a fan whose beams wave up and down one
// after another, 3 a tunnel of beams on a turning cone, 4 a sheet of light
// (a beam scanned so fast it looks like a plane). Patterns 1 to 3 have up
// to LASER_BEAMS beams, and projector j's beam k is row
// j * LASER_BEAMS + k of the laserBeams image.

#ifndef STAGE_LASERS_GLSL
#define STAGE_LASERS_GLSL

#include "lasers_block.glsl"

struct Projector {
    vec3 origin;
    int pattern;
    vec3 axis;
    int beams;
    vec3 up;
    float spread;
    vec3 color;
    float phase;
};

Projector laserProjector(int j) {
    vec4 o, a, u, c;
    if (j == 0)      { o = lasers.origin0; a = lasers.axis0; u = lasers.up0; c = lasers.color0; }
    else if (j == 1) { o = lasers.origin1; a = lasers.axis1; u = lasers.up1; c = lasers.color1; }
    else if (j == 2) { o = lasers.origin2; a = lasers.axis2; u = lasers.up2; c = lasers.color2; }
    else             { o = lasers.origin3; a = lasers.axis3; u = lasers.up3; c = lasers.color3; }
    Projector p;
    p.origin = o.xyz;
    p.pattern = int(o.w + 0.5);
    p.axis = normalize(a.xyz);
    p.beams = min(int(a.w + 0.5), LASER_BEAMS);
    p.up = u.xyz;
    p.spread = u.w;
    p.color = c.rgb;
    p.phase = c.w;
    return p;
}

// The projector's right and up, square to its axis.
void laserFrame(Projector p, out vec3 right, out vec3 up) {
    right = normalize(cross(p.axis, p.up));
    up = cross(right, p.axis);
}

// Beam k's direction at time t, for patterns 1 to 3.
vec3 laserBeamDirection(Projector p, int k, float t) {
    vec3 right, up;
    laserFrame(p, right, up);
    float n = float(p.beams);
    float across = p.beams > 1 ? 2.0 * float(k) / (n - 1.0) - 1.0 : 0.0;
    if (p.pattern == 3) {
        float a = 6.2831853 * float(k) / n + 1.3 * t + p.phase;
        return normalize(p.axis * cos(p.spread) + sin(p.spread) * (right * cos(a) + up * sin(a)));
    }
    float a = p.spread * across;
    float tilt;
    if (p.pattern == 1) {
        float roll = 0.35 * sin(0.31 * t + p.phase);
        vec3 r = right * cos(roll) + up * sin(roll);
        up = cross(r, p.axis);
        right = r;
        tilt = 0.22 * sin(0.7 * t + p.phase);
    } else {
        tilt = 0.3 * sin(2.0 * t + 0.6 * float(k) + p.phase);
    }
    return normalize(p.axis * cos(a) + right * sin(a) + up * tan(tilt));
}

// A sheet's centre line and normal at time t (pattern 4): the plane through
// the aperture spanned by the projector's right and its axis tilted up and
// down slowly; the sheet covers `spread` either side of the centre line.
void laserSheet(Projector p, float t, out vec3 centre, out vec3 normal) {
    vec3 right, up;
    laserFrame(p, right, up);
    float tilt = 0.12 * sin(0.5 * t + p.phase);
    centre = normalize(p.axis * cos(tilt) + up * sin(tilt));
    normal = normalize(cross(right, centre));
}

#endif
