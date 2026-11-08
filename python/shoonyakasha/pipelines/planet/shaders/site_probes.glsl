// The nearest site's sky visibility (site_probes.comp, handed over by
// shoonyakasha.planet.world): a grid of SITE_PROBE_COUNTS probes in the
// site's frame, each texel of siteVisibility (x across, y * counts.z + z
// down) its visibility's L1 spherical harmonics, zeros for a probe inside
// the geometry. Include after camera.glsl, which declares `site` and
// `siteVisibility`.
//
// siteSkyShare: the share of the sky a surface at `relativePos` facing
// `normal` sees, the visibility convolved with the cosine about the normal
// over pi, at the point moved SITE_PROBE_OFFSET m along the normal: the
// eight probes around it blended by their distances, those inside the
// geometry left out; 1 with no site (site.origin.w 0), where no probe
// around it is outside the geometry, and fading to 1 over the grid's
// outermost SITE_PROBE_FADE cell beyond its last probes.

const ivec3 SITE_PROBE_COUNTS = ivec3(32, 12, 32);
const float SITE_PROBE_OFFSET = 1.0;   // m
const float SITE_PROBE_FADE = 1.0;     // of a cell

float siteSkyShare(vec3 relativePos, vec3 normal) {
    if (site.origin.w < 0.5) return 1.0;
    vec3 d = relativePos + normal * SITE_PROBE_OFFSET - site.origin.xyz;
    vec3 q = vec3(dot(d, site.east.xyz), dot(d, site.up.xyz), dot(d, site.north.xyz));
    vec3 n = vec3(dot(normal, site.east.xyz), dot(normal, site.up.xyz), dot(normal, site.north.xyz));
    vec3 g = (q - site.gridMin.xyz) / site.spacing.xyz - 0.5;
    vec3 last = vec3(SITE_PROBE_COUNTS - 1);
    vec3 beyond = max(max(-g, g - last), vec3(0.0));
    float inside = 1.0 - smoothstep(0.0, SITE_PROBE_FADE, max(beyond.x, max(beyond.y, beyond.z)));
    if (inside <= 0.0) return 1.0;
    g = clamp(g, vec3(0.0), last);
    ivec3 i0 = ivec3(min(floor(g), last - 1.0));
    vec3 f = g - vec3(i0);
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int k = 0; k < 8; ++k) {
        ivec3 o = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        ivec3 i = i0 + o;
        vec4 c = texelFetch(siteVisibility, ivec2(i.x, i.y * SITE_PROBE_COUNTS.z + i.z), 0);
        vec3 t = mix(1.0 - f, f, vec3(o));
        float w = t.x * t.y * t.z * step(1e-4, c.x);
        sum += w * c;
        total += w;
    }
    if (total < 1e-4) return 1.0;
    vec4 c = sum / total;
    // Cosine-convolved L1: pi L00 Y00 + 2 pi / 3 L1 . Y1(n), over pi.
    float share = 0.282095 * c.x + (2.0 / 3.0) * 0.488603 * dot(c.yzw, n);
    return mix(1.0, clamp(share, 0.0, 1.0), inside);
}
