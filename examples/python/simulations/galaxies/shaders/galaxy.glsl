// The galaxies' records and the acceleration of their potentials, shared by
// galaxies.comp and step.comp (model.py computes the same in NumPy).
//
// A galaxy is a Hernquist halo, a Hernquist bulge and a Miyamoto-Nagai disc
// whose axis is `spin`, all centred on `position`. `previous` is the centre
// one step earlier.

struct Galaxy {
    vec4 positionMass;   // centre, total mass
    vec4 velocity;
    vec4 previous;
    vec4 spin;           // disc axis (unit)
    vec4 shape;          // halo scale, bulge scale, disc a, disc b
    vec4 masses;         // halo, bulge, disc
};

vec3 hernquist(vec3 d, float mass, float scale, float softening) {
    float r = sqrt(dot(d, d) + softening * softening);
    return -mass / (r * (r + scale) * (r + scale)) * d;
}

vec3 miyamotoNagai(vec3 d, vec3 spin, float mass, float a, float b, float softening) {
    float z = dot(d, spin);
    float zeta = sqrt(z * z + b * b);
    float bigD2 = dot(d, d) - z * z + (a + zeta) * (a + zeta) + softening * softening;
    float bigD = sqrt(bigD2);
    return -mass / (bigD2 * bigD) * (d + (a / zeta * z) * spin);
}

// Acceleration from galaxy `g` at offset `d` from its centre.
vec3 galaxyAcceleration(Galaxy g, vec3 d, float softening) {
    return hernquist(d, g.masses.x, g.shape.x, softening)
         + hernquist(d, g.masses.y, g.shape.y, softening)
         + miyamotoNagai(d, g.spin.xyz, g.masses.z, g.shape.z, g.shape.w, softening);
}
