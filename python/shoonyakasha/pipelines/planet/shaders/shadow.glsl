// The sun's shadow at a surface: 1 lit, 0 in shadow. Include after
// camera.glsl and atmosphere.glsl.
//
// horizonShadow: the shadow of distant terrain, from the sine of the
// horizon towards the sun's azimuth that chunk_build.comp measured at the
// vertices (300 m to 40 km away): lit where the sun stands above it,
// softened over about a degree near the camera and up to about four beyond
// 60 km, where the vertices are kilometres apart (about a twentieth of
// their distance) and a sharp edge would follow their triangles.

float horizonShadow(float sinHorizon, vec3 radial, float distance) {
    float soft = 0.015 + 0.06 * smoothstep(5000.0, 60000.0, distance);
    return smoothstep(-soft, soft, dot(atmosphere.sun.xyz, radial) - sinHorizon);
}

// sunShadow: the shadow map's. The cascade is the first whose far distance (shadow.splits) is beyond
// the point's distance along the view. The point is pushed along its
// normal by two of that cascade's texels, so a surface does not shadow
// itself, and compared with the map at SHADOW_TAPS places on a disk of
// SHADOW_SOFT metres (at least SHADOW_MIN_TEXELS texels) across its
// radius, turned by a noise on the pixel (each a filtered comparison of
// four texels). Over the last SHADOW_BLEND of a
// cascade's far distance the shadow blends towards the next cascade's
// (where the point is within that cascade's map). Over the last tenth of
// the last cascade the shadow fades out; beyond it everything is lit.

const float SHADOW_BLEND = 0.2;
const float SHADOW_SOFT = 0.12;          // m, the disk's radius
const float SHADOW_MIN_TEXELS = 1.5;     // the disk's least radius, in texels
const int SHADOW_TAPS = 12;
const vec2 SHADOW_DISK[SHADOW_TAPS] = vec2[SHADOW_TAPS](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
    vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
    vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

// The shadow of cascade `c` at the point, 1 lit; `inside` false (and 1
// returned) where the point is outside the cascade's map.
float cascadeShadow(int c, vec3 relativePos, vec3 normal, out bool inside) {
    vec3 p = relativePos + normal * (2.0 * shadow.texel[c]);
    vec4 clip = shadow.viewProj[c] * vec4(p, 1.0);
    vec2 uv = clip.xy * 0.5 + 0.5;
    inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
    if (!inside) return 1.0;
    float radius = max(SHADOW_SOFT / shadow.texel[c], SHADOW_MIN_TEXELS) / shadow.settings.y;
    float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    mat2 turn = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    float lit = 0.0;
    for (int i = 0; i < SHADOW_TAPS; ++i) {
        lit += texture(sunShadowMap, vec4(uv + turn * SHADOW_DISK[i] * radius, float(c), clip.z));
    }
    return lit / float(SHADOW_TAPS);
}

float sunShadow(vec3 relativePos, vec3 normal) {
    if (shadow.settings.x == 0.0) return 1.0;
    float depth = -(camera.view * vec4(relativePos, 1.0)).z;
    if (depth >= shadow.splits.w) return 1.0;
    int c = depth < shadow.splits.x ? 0 : depth < shadow.splits.y ? 1 : depth < shadow.splits.z ? 2 : 3;
    bool inside;
    float lit = cascadeShadow(c, relativePos, normal, inside);
    if (!inside) return 1.0;
    float far = shadow.splits[c];
    if (c < 3) {
        float blend = smoothstep((1.0 - SHADOW_BLEND) * far, far, depth);
        if (blend > 0.0) {
            bool nextInside;
            float next = cascadeShadow(c + 1, relativePos, normal, nextInside);
            if (nextInside) lit = mix(lit, next, blend);
        }
    }
    return mix(lit, 1.0, smoothstep(0.9 * shadow.splits.w, shadow.splits.w, depth));
}
