// The sky probe: the light arriving at the camera from every direction
// (sky_probe.comp), stored on an octahedral map of SKY_PROBE_SIZE texels a
// side. skyProbe holds SKY_PROBE_LEVELS such maps one above the other, the
// first the light itself and each next one prefiltered for a GGX lobe of a
// rougher surface (sky_probe_filter.comp, roughness level / (levels - 1));
// skySh holds its L2 spherical harmonics, one coefficient per texel of a
// row (sky_sh.comp: L00, L1-1, L10, L11, L2-2, L2-1, L20, L21, L22).
//
// Declare `skyProbe` and `skySh` before using skyReflection and
// skyIrradiance; the octahedral mapping needs neither.

const int SKY_PROBE_SIZE = 64;
const int SKY_PROBE_LEVELS = 5;

// A unit direction's place on the octahedral map, 0 .. 1 each way.
vec2 skyOctEncode(vec3 d) {
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    vec2 p = d.xz;
    if (d.y < 0.0) p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    return p * 0.5 + 0.5;
}

// The unit direction at a place on the octahedral map.
vec3 skyOctDecode(vec2 uv) {
    vec2 f = uv * 2.0 - 1.0;
    vec3 d = vec3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
    float t = max(-d.y, 0.0);
    d.x += d.x >= 0.0 ? -t : t;
    d.z += d.z >= 0.0 ? -t : t;
    return normalize(d);
}

// The L2 spherical harmonics' basis at the unit direction `d`, in skySh's order.
void skyShBasis(vec3 d, out float y[9]) {
    y[0] = 0.282095;
    y[1] = 0.488603 * d.y;
    y[2] = 0.488603 * d.z;
    y[3] = 0.488603 * d.x;
    y[4] = 1.092548 * d.x * d.y;
    y[5] = 1.092548 * d.y * d.z;
    y[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
    y[7] = 1.092548 * d.x * d.z;
    y[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

#ifdef SKY_PROBE_SAMPLERS
// The probe's light from the direction `d` for a surface of `roughness`
// (0 .. 1): its two nearest levels, each sampled half a texel inside its
// map, blended.
vec3 skyReflection(vec3 d, float roughness) {
    float level = clamp(roughness, 0.0, 1.0) * float(SKY_PROBE_LEVELS - 1);
    int l0 = int(floor(level));
    int l1 = min(l0 + 1, SKY_PROBE_LEVELS - 1);
    vec2 at = clamp(skyOctEncode(d) * float(SKY_PROBE_SIZE), vec2(0.5), vec2(float(SKY_PROBE_SIZE) - 0.5));
    vec2 size = vec2(float(SKY_PROBE_SIZE), float(SKY_PROBE_SIZE * SKY_PROBE_LEVELS));
    vec3 a = texture(skyProbe, (at + vec2(0.0, float(l0 * SKY_PROBE_SIZE))) / size).rgb;
    vec3 b = texture(skyProbe, (at + vec2(0.0, float(l1 * SKY_PROBE_SIZE))) / size).rgb;
    return mix(a, b, level - float(l0));
}

// The irradiance (illuminance per colour) on a surface facing `n`, from the
// probe's harmonics convolved with the cosine (Ramamoorthi and Hanrahan 2001).
vec3 skyIrradiance(vec3 n) {
    vec3 c[9];
    for (int k = 0; k < 9; ++k) c[k] = texelFetch(skySh, ivec2(k, 0), 0).rgb;
    const float c1 = 0.429043, c2 = 0.511664, c3 = 0.743125, c4 = 0.886227, c5 = 0.247708;
    vec3 e = c1 * c[8] * (n.x * n.x - n.y * n.y) + c3 * c[6] * n.z * n.z + c4 * c[0] - c5 * c[6]
           + 2.0 * c1 * (c[4] * n.x * n.y + c[7] * n.x * n.z + c[5] * n.y * n.z)
           + 2.0 * c2 * (c[3] * n.x + c[1] * n.y + c[2] * n.z);
    return max(e, vec3(0.0));
}
#endif
