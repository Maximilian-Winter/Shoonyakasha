#version 450
// Ground-truth ambient occlusion (Jimenez et al. 2016): for two slices
// through the view direction, find the horizon on each side of the pixel in
// the depth buffer and integrate the cosine-weighted visible arc. The slice
// directions follow a 4x4 pattern that ao_denoise.frag averages out.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDepth;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out float outAO;

const float PI = 3.14159265;
const float HALF_PI = 1.57079633;
const int SLICES = 2;
const int STEPS = 8;

vec3 viewPositionAt(vec2 uv) {
    return viewPositionFromDepth(camera.invProj, uv, textureLod(gDepth, uv, 0.0).r);
}

void main() {
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    if (depth >= 1.0 || settings.aoRadius <= 0.0) {
        outAO = 1.0;
        return;
    }

    vec3 P = viewPositionFromDepth(camera.invProj, fragTexCoord, depth);
    vec3 V = normalize(-P);
    vec3 N = normalize(mat3(camera.view) * octDecode(textureLod(gNormal, fragTexCoord, 0.0).xy));

    // The world radius as a length on screen, in uv.
    float radiusUV = settings.aoRadius * abs(camera.proj[1][1]) * 0.5 / max(-P.z, 1e-3);
    vec2 texel = 1.0 / vec2(textureSize(gDepth, 0));
    radiusUV = min(radiusUV, 0.25);   // keep samples near the pixel when it is very close
    if (radiusUV < texel.y) {
        outAO = 1.0;
        return;
    }

    float minOffset = 1.5 * texel.y / radiusUV;

    // 4x4 interleaved slice rotation and step jitter.
    ivec2 pixel = ivec2(gl_FragCoord.xy) & 3;
    float rotation = (float(pixel.x * 4 + pixel.y) + 0.5) / 16.0;
    float jitter = fract(float(pixel.y * 4 + pixel.x) * 0.61803398 + 0.3);

    float visibility = 0.0;
    for (int s = 0; s < SLICES; ++s) {
        float phi = (float(s) + rotation) * PI / float(SLICES);
        vec2 dirUV = vec2(cos(phi), sin(phi)) * vec2(texel.x / texel.y, 1.0);   // round on screen

        // The slice's direction in view space, from a point beside P at P's depth.
        vec3 side = viewPositionFromDepth(camera.invProj, fragTexCoord + dirUV * texel.y, depth);
        vec3 direction = normalize(side - P);
        vec3 orthoDirection = direction - dot(direction, V) * V;
        vec3 axis = normalize(cross(orthoDirection, V));
        vec3 projN = N - axis * dot(N, axis);
        float projLength = length(projN);
        float cosN = clamp(dot(projN, V) / max(projLength, 1e-4), -1.0, 1.0);
        float n = sign(dot(orthoDirection, projN)) * acos(cosN);

        float horizonCos[2] = float[](-1.0, -1.0);
        for (int sideIndex = 0; sideIndex < 2; ++sideIndex) {
            float sgn = sideIndex == 0 ? 1.0 : -1.0;
            for (int i = 0; i < STEPS; ++i) {
                float t = (float(i) + jitter) / float(STEPS);
                // Denser near the pixel, but never within 1.5 pixels of it: a
                // sample of the pixel itself reads as a horizon straight up.
                float offset = mix(minOffset, 1.0, t * t);
                // On a whole pixel, so the depth read belongs to the position
                // reconstructed; otherwise a sloped surface jitters up and
                // down and the highest jitter becomes a false horizon.
                vec2 uv = (floor((fragTexCoord + sgn * dirUV * radiusUV * offset) / texel) + 0.5) * texel;
                if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
                vec3 delta = viewPositionAt(uv) - P;
                float len = length(delta);
                float c = dot(delta, V) / max(len, 1e-4);
                // Fade out occluders towards the radius so the effect has no edge.
                float falloff = clamp(2.0 - 2.0 * len / settings.aoRadius, 0.0, 1.0);
                horizonCos[sideIndex] = max(horizonCos[sideIndex], mix(-1.0, c, falloff));
            }
        }

        float h0 = n + max(-acos(horizonCos[1]) - n, -HALF_PI);
        float h1 = n + min(acos(horizonCos[0]) - n, HALF_PI);
        float arc0 = cosN + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n);
        float arc1 = cosN + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n);
        visibility += projLength * 0.25 * (arc0 + arc1);
    }
    visibility /= float(SLICES);
    outAO = pow(clamp(visibility, 0.0, 1.0), settings.aoIntensity);
}
