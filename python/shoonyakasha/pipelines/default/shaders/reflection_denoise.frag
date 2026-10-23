#version 450
// The ray-traced reflections of lighting_rt.frag, filtered and added to the
// HDR target (pass ReflectionDenoise). Each pixel traced one ray, spread by
// its roughness; this averages it with the neighbours on the same surface
// (near the pixel's plane, with a similar normal, also traced), over a
// radius that grows with roughness, then multiplies the average by the
// pixel's own reflectance. TAA averages what is left over frames.
//
//   reflection       rgb  traced light minus the environment's reflection it
//                         replaces, divided by the reflectance, times the
//                         trace's weight
//                    a    1 where the surface is traced
//   reflectionScale  rgb  the reflectance
//                    a    the roughness traced

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D reflection;
layout(set = 0, binding = 1) uniform sampler2D reflectionScale;
layout(set = 0, binding = 2) uniform sampler2D gDepth;
layout(set = 0, binding = 3) uniform sampler2D gNormal;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// The filter's radius in pixels: RADIUS_PER_SPREAD times the spread of the
// reflection rays (roughness squared, as tracedReflection spreads them), at
// most MAX_RADIUS.
const float RADIUS_PER_SPREAD = 40.0;
const float MAX_RADIUS = 6.0;

vec3 viewPositionAt(ivec2 p, vec2 size) {
    return viewPositionFromDepth(camera.invProj, (vec2(p) + 0.5) / size, texelFetch(gDepth, p, 0).r);
}

void main() {
    ivec2 center = ivec2(gl_FragCoord.xy);
    vec4 own = texelFetch(reflection, center, 0);
    if (own.a == 0.0) {
        outColor = vec4(0.0);
        return;
    }
    vec4 scale = texelFetch(reflectionScale, center, 0);
    float radius = min(scale.a * scale.a * RADIUS_PER_SPREAD, MAX_RADIUS);
    if (radius < 0.5) {
        outColor = vec4(own.rgb * scale.rgb, 0.0);
        return;
    }

    ivec2 size = textureSize(reflection, 0);
    vec3 P = viewPositionAt(center, vec2(size));
    vec3 N = octDecode(texelFetch(gNormal, center, 0).xy);
    vec3 viewN = mat3(camera.view) * N;
    // How far a neighbour may lie off this pixel's plane and still count.
    float tolerance = 0.01 * -P.z + 0.005;

    // 5 x 5 taps spread over the radius, Gaussian-weighted.
    float spacing = radius * 0.5;
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            ivec2 p = center + ivec2(round(vec2(x, y) * spacing));
            if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) continue;
            vec4 s = texelFetch(reflection, p, 0);
            if (s.a == 0.0) continue;
            float plane = abs(dot(viewN, viewPositionAt(p, vec2(size)) - P));
            float facing = max(dot(N, octDecode(texelFetch(gNormal, p, 0).xy)), 0.0);
            float w = exp(-0.3 * float(x * x + y * y))
                    * clamp(1.0 - plane / tolerance, 0.0, 1.0)
                    * pow(facing, 16.0);
            sum += s.rgb * w;
            total += w;
        }
    }
    // The centre tap always counts, with weight 1.
    outColor = vec4(sum / total * scale.rgb, 0.0);
}
