//
// sk/noise.glsl - hashing and value noise
//
//     #include "sk/noise.glsl"
//

#ifndef SK_NOISE_GLSL
#define SK_NOISE_GLSL

// Integer hash of the lattice cell containing p, in [0, 1]. Integer rather
// than fract(sin(...)) or fract(x * y), so neighbouring cells hash
// consistently regardless of how the compiler fuses the arithmetic.
float hash(vec2 p) {
    uvec2 q = uvec2(ivec2(floor(p)));
    uint h = (q.x * 1597334677u) ^ (q.y * 3812015801u);
    h = h * 747796405u + 2891336453u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return float(h) * (1.0 / 4294967295.0);
}

// Value noise in [0, 1], smoothly interpolated between lattice cells.
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Five octaves of value noise, in roughly [0, 1].
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

#endif
