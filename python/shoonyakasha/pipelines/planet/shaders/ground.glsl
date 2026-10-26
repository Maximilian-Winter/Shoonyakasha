// The ground's textures (groundSet in pipeline.json, bound as set
// GROUND_SET): an albedo and a normal map per layer (biome.glsl's), sampled
// in planet space by triplanar projection at two scales. Include after
// biome.glsl.
//
//     offsets[body * 2 + k]  xyz: the camera's position relative to the body's
//                            centre over scale k, less its whole part (in
//                            doubles, so the tiling stays steady far from the
//                            centre); a point's texture coordinates are this
//                            plus its position relative to the camera over
//                            the scale
//     means[layer]           rgb: the layer's mean albedo (linear)
//     settings               x: the textures' strength (0 off .. 1),
//                            y: the normal maps' strength,
//                            z, w: the near and far scales (m per tile)
//
// A layer's albedo is its procedural colour times its texture over the
// texture's mean, near scale times far. Each scale fades out as its tiles
// shrink on the screen, from GROUND_FADE_PIXELS.x pixels across to
// GROUND_FADE_PIXELS.y, before their repetition shows: far off the ground
// is its procedural colour again.

layout(set = GROUND_SET, binding = 0) uniform GroundParams {
    vec4 offsets[4];
    vec4 means[7];
    vec4 settings;
} ground;

layout(set = GROUND_SET, binding = 1) uniform sampler2D groundAlbedo0;
layout(set = GROUND_SET, binding = 2) uniform sampler2D groundAlbedo1;
layout(set = GROUND_SET, binding = 3) uniform sampler2D groundAlbedo2;
layout(set = GROUND_SET, binding = 4) uniform sampler2D groundAlbedo3;
layout(set = GROUND_SET, binding = 5) uniform sampler2D groundAlbedo4;
layout(set = GROUND_SET, binding = 6) uniform sampler2D groundAlbedo5;
layout(set = GROUND_SET, binding = 7) uniform sampler2D groundAlbedo6;
layout(set = GROUND_SET, binding = 8) uniform sampler2D groundNormal0;
layout(set = GROUND_SET, binding = 9) uniform sampler2D groundNormal1;
layout(set = GROUND_SET, binding = 10) uniform sampler2D groundNormal2;
layout(set = GROUND_SET, binding = 11) uniform sampler2D groundNormal3;
layout(set = GROUND_SET, binding = 12) uniform sampler2D groundNormal4;
layout(set = GROUND_SET, binding = 13) uniform sampler2D groundNormal5;
layout(set = GROUND_SET, binding = 14) uniform sampler2D groundNormal6;

const float GROUND_MIN_WEIGHT = 0.02;   // layers and projections below this are not sampled
const vec2 GROUND_FADE_PIXELS = vec2(48.0, 12.0);

// How much a scale's texture shows for coordinates (in tiles) changing by
// `dx` and `dy` per pixel: 1 while a tile is GROUND_FADE_PIXELS.x pixels
// across or more, 0 at GROUND_FADE_PIXELS.y or less.
float groundScaleFade(vec3 dx, vec3 dy) {
    float pixelsPerTile = 1.0 / max(max(length(dx), length(dy)), 1e-6);
    return smoothstep(GROUND_FADE_PIXELS.y, GROUND_FADE_PIXELS.x, pixelsPerTile);
}

// The textures are sampled in branches that differ between neighbouring
// pixels, so with gradients taken before them (`g`: d/dx, d/dy of `uv`).
vec3 groundAlbedoTexel(int layer, vec2 uv, vec4 g) {
    switch (layer) {
        case 0: return textureGrad(groundAlbedo0, uv, g.xy, g.zw).rgb;
        case 1: return textureGrad(groundAlbedo1, uv, g.xy, g.zw).rgb;
        case 2: return textureGrad(groundAlbedo2, uv, g.xy, g.zw).rgb;
        case 3: return textureGrad(groundAlbedo3, uv, g.xy, g.zw).rgb;
        case 4: return textureGrad(groundAlbedo4, uv, g.xy, g.zw).rgb;
        case 5: return textureGrad(groundAlbedo5, uv, g.xy, g.zw).rgb;
        default: return textureGrad(groundAlbedo6, uv, g.xy, g.zw).rgb;
    }
}

// The tangent-space normal (OpenGL convention, +y up the image).
vec3 groundNormalTexel(int layer, vec2 uv, vec4 g) {
    vec3 n;
    switch (layer) {
        case 0: n = textureGrad(groundNormal0, uv, g.xy, g.zw).xyz; break;
        case 1: n = textureGrad(groundNormal1, uv, g.xy, g.zw).xyz; break;
        case 2: n = textureGrad(groundNormal2, uv, g.xy, g.zw).xyz; break;
        case 3: n = textureGrad(groundNormal3, uv, g.xy, g.zw).xyz; break;
        case 4: n = textureGrad(groundNormal4, uv, g.xy, g.zw).xyz; break;
        case 5: n = textureGrad(groundNormal5, uv, g.xy, g.zw).xyz; break;
        default: n = textureGrad(groundNormal6, uv, g.xy, g.zw).xyz; break;
    }
    return n * 2.0 - 1.0;
}

float groundLuminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// The weights of the three planar projections for the normal `n`,
// sharpened so most of the ground takes one or two.
vec3 groundProjectionWeights(vec3 n) {
    vec3 w = pow(abs(n), vec3(6.0));
    return w / (w.x + w.y + w.z);
}

// Layer weights (summing to 1) and procedural colours in, the textured
// albedo and the normal with the layers' normal maps out. `relative` is the
// point's position relative to the camera, `body` the body's index,
// `normal` the surface's unit normal in planet space. The moon's layer
// takes its texture's brightness alone.
void groundTextured(float weights[GROUND_LAYERS], vec3 colours[GROUND_LAYERS], vec3 relative, int body,
                    vec3 normal, out vec3 albedo, out vec3 shadedNormal) {
    albedo = vec3(0.0);
    for (int i = 0; i < GROUND_LAYERS; ++i) albedo += weights[i] * colours[i];
    shadedNormal = normal;

    // Coordinates and their gradients first, where every pixel of a quad runs.
    vec3 nearUv = ground.offsets[body * 2].xyz + relative / ground.settings.z;
    vec3 farUv = ground.offsets[body * 2 + 1].xyz + relative / ground.settings.w;
    vec3 nearDx = dFdx(nearUv), nearDy = dFdy(nearUv);
    vec3 farDx = dFdx(farUv), farDy = dFdy(farUv);
    vec2 nearPlanes[3] = vec2[3](nearUv.zy, nearUv.xz, nearUv.xy);
    vec2 farPlanes[3] = vec2[3](farUv.zy, farUv.xz, farUv.xy);
    vec4 nearGrads[3] = vec4[3](vec4(nearDx.zy, nearDy.zy), vec4(nearDx.xz, nearDy.xz), vec4(nearDx.xy, nearDy.xy));
    vec4 farGrads[3] = vec4[3](vec4(farDx.zy, farDy.zy), vec4(farDx.xz, farDy.xz), vec4(farDx.xy, farDy.xy));
    float nearFade = groundScaleFade(nearDx, nearDy);
    float farFade = groundScaleFade(farDx, farDy);
    float strength = ground.settings.x;
    if (strength <= 0.0 || farFade <= 0.0) return;
    vec3 projection = groundProjectionWeights(normal);

    vec3 textured = vec3(0.0);
    vec3 tangent[3] = vec3[3](vec3(0.0), vec3(0.0), vec3(0.0));
    float total = 0.0;
    for (int i = 0; i < GROUND_LAYERS; ++i) {
        float w = weights[i];
        if (w < GROUND_MIN_WEIGHT) continue;
        vec3 mean = max(ground.means[i].rgb, vec3(1e-3));
        vec3 detail = vec3(0.0);
        for (int p = 0; p < 3; ++p) {
            float wp = projection[p];
            if (wp < GROUND_MIN_WEIGHT) continue;
            vec3 near = nearFade > 0.0 ? groundAlbedoTexel(i, nearPlanes[p], nearGrads[p]) / mean : vec3(1.0);
            vec3 far = groundAlbedoTexel(i, farPlanes[p], farGrads[p]) / mean;
            detail += wp * mix(vec3(1.0), near, nearFade) * mix(vec3(1.0), far, farFade);
            vec3 bumps = nearFade > 0.0 ? groundNormalTexel(i, nearPlanes[p], nearGrads[p]) : vec3(0.0, 0.0, 1.0);
            tangent[p] += w * vec3(bumps.xy * nearFade, bumps.z);
        }
        if (i == GROUND_MOON) detail = vec3(groundLuminance(detail));
        textured += w * colours[i] * detail;
        total += w;
    }
    // The layers too light to sample keep their procedural colour and a flat normal.
    vec3 untextured = vec3(0.0);
    for (int i = 0; i < GROUND_LAYERS; ++i) {
        if (weights[i] < GROUND_MIN_WEIGHT) untextured += weights[i] * colours[i];
    }
    albedo = mix(albedo, textured + untextured, strength);
    for (int p = 0; p < 3; ++p) tangent[p].z += 1.0 - total;

    // Whiteout blend of the projections' tangent normals onto the surface normal.
    float bump = ground.settings.y * strength;
    vec3 tx = vec3(tangent[0].xy * bump + normal.zy, abs(tangent[0].z) * normal.x);
    vec3 ty = vec3(tangent[1].xy * bump + normal.xz, abs(tangent[1].z) * normal.y);
    vec3 tz = vec3(tangent[2].xy * bump + normal.xy, abs(tangent[2].z) * normal.z);
    vec3 blended = tx.zyx * projection.x + ty.xzy * projection.y + tz.xyz * projection.z;
    shadedNormal = total > 0.0 ? normalize(blended) : normal;
}
