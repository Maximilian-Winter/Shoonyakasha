// The sun's shadow at a surface: 1 lit, 0 in shadow. Include after
// camera.glsl.
//
// The cascade is the first whose far distance (shadow.splits) is beyond
// the point's distance along the view. The point is pushed along its
// normal by two of that cascade's texels, so a surface does not shadow
// itself, and compared with the map at nine places a texel apart (each a
// filtered comparison of four texels). Over the last tenth of the last
// cascade the shadow fades out; beyond it everything is lit.

float sunShadow(vec3 relativePos, vec3 normal) {
    if (shadow.settings.x == 0.0) return 1.0;
    float depth = -(camera.view * vec4(relativePos, 1.0)).z;
    if (depth >= shadow.splits.w) return 1.0;
    int c = depth < shadow.splits.x ? 0 : depth < shadow.splits.y ? 1 : depth < shadow.splits.z ? 2 : 3;
    vec3 p = relativePos + normal * (2.0 * shadow.texel[c]);
    vec4 clip = shadow.viewProj[c] * vec4(p, 1.0);
    vec2 uv = clip.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    float step = 1.0 / shadow.settings.y;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += texture(sunShadowMap, vec4(uv + vec2(x, y) * step, float(c), clip.z));
        }
    }
    lit /= 9.0;
    return mix(lit, 1.0, smoothstep(0.9 * shadow.splits.w, shadow.splits.w, depth));
}
