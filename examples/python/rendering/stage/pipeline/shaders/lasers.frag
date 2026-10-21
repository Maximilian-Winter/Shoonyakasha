#version 460
// glslc: --target-env=vulkan1.2
// The lasers over the image (pass Lasers), added to the HDR target:
// - beams (laserBeams, traced by laser_trace.comp): the light the haze and
//   smoke scatter towards the eye where the view ray passes the beam. A
//   beam is a thin Gaussian tube, widened to the size of a pixel where it
//   is thinner, so its light stays the same as it gets further away; the
//   light along the view ray through it is the power times the fog's
//   density and the phase function, over the tube's width and the sine of
//   the angle between ray and beam;
// - sheets (pattern 4): the light of a plane of light where the view ray
//   crosses it, the power spread over the sheet's arc at that distance, and
//   a ray back to the aperture for its shadow;
// - where beams and sheets meet surfaces, a dot or a line of their colour.
// Everything is dimmed by the fog between it and the eye.

#extension GL_EXT_ray_query : require

#include "common.glsl"
#include "fog.glsl"
#include "lasers_block.glsl"

layout(set = 0, binding = 0) uniform sampler2D laserBeams;
layout(set = 0, binding = 1) uniform sampler2D gDepth;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gNormal;
layout(set = 0, binding = 4) uniform sampler3D fogIntegrated;
layout(set = 0, binding = 5) uniform Fog { FOG_BLOCK } fog;
layout(set = 0, binding = 6) uniform Lasers { LASERS_BLOCK } lasers;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform accelerationStructureEXT scene;

#include "lasers.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

float density(vec3 p) {
    return fogDensity(p, fog.media, fog.motion, fog.wind, fog.hall,
                      fog.machine0, fog.machine1, fog.machine2, fog.machine3,
                      fog.machineDir0, fog.machineDir1, fog.machineDir2, fog.machineDir3, fog.burst) * fog.grid.w;
}

// The share of light from p that reaches the eye through the fog.
float transmittance(vec3 p) {
    float viewDepth = -(camera.view * vec4(p, 1.0)).z;
    float slices = float(textureSize(fogIntegrated, 0).z);
    vec4 clip = camera.proj * camera.view * vec4(p, 1.0);
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (fog.grid.w <= 0.0) return 1.0;
    return fogAt(fogIntegrated, clamp(uv, 0.0, 1.0), depthSlice(viewDepth, slices, fog.grid), slices).a;
}

// Whether anything lies between p and q.
bool blocked(vec3 p, vec3 q) {
    vec3 d = q - p;
    float length_ = length(d);
    if (length_ <= 0.05) return false;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                          0xFF, p, 0.0, d / length_, length_ - 0.05);
    while (rayQueryProceedEXT(query)) {}
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

void main() {
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    bool surface = depth < 1.0;
    vec3 viewPos = viewPositionFromDepth(camera.invProj, fragTexCoord, surface ? depth : 0.99999);
    vec3 eye = camera.position.xyz;
    vec3 surfacePos = (camera.invView * vec4(viewPos, 1.0)).xyz;
    vec3 v = normalize(surfacePos - eye);
    float reach = surface ? distance(surfacePos, eye) : lasers.params.w;
    // A pixel's width per metre of distance.
    float pixelAngle = 2.0 / (abs(camera.proj[1][1]) * camera.resolution.y);
    float radius = lasers.params.y;
    float g = fog.media.w;
    vec3 albedo = surface ? textureLod(gAlbedo, fragTexCoord, 0.0).rgb : vec3(0.0);
    vec3 N = surface ? octDecode(textureLod(gNormal, fragTexCoord, 0.0).xy) : vec3(0.0);

    vec3 light = vec3(0.0);
    int rows = LASER_PROJECTORS * LASER_BEAMS;
    for (int b = 0; b < rows; ++b) {
        vec4 start = texelFetch(laserBeams, ivec2(0, b), 0);
        if (start.w <= 0.0) continue;
        vec4 heading = texelFetch(laserBeams, ivec2(1, b), 0);
        vec3 o = start.xyz, d = heading.xyz;
        vec3 color = laserProjector(int(heading.w + 0.5)).color;

        // The points of the view ray and the beam closest to each other.
        vec3 w0 = eye - o;
        float cosA = dot(v, d);
        float sin2 = 1.0 - cosA * cosA;
        if (sin2 > 1e-6) {
            float s = clamp((dot(d, w0) - cosA * dot(v, w0)) / sin2, 0.0, start.w);
            vec3 q = o + d * s;
            float t = dot(q - eye, v);
            if (t > 0.0 && t < reach) {
                float miss = length(eye + v * t - q);
                float width = max(radius, pixelAngle * t * 0.7);
                float tube = exp(-0.5 * miss * miss / (width * width));
                if (tube > 1e-3) {
                    float sigma = density(q);
                    if (sigma > 0.0) {
                        light += color * sigma * phaseHG(dot(d, -v), g) * tube
                               / (2.5066283 * width * max(sqrt(sin2), 0.03)) * transmittance(q);
                    }
                }
            }
        }
        // Its dot where it lands on this pixel's surface.
        if (surface && start.w < lasers.params.w * 0.999) {
            vec3 hit = o + d * start.w;
            float spot = max(radius * 2.0, pixelAngle * reach * 0.8);
            float off = distance(surfacePos, hit);
            if (off < spot * 4.0) {
                float shape = exp(-0.5 * off * off / (spot * spot)) * (radius * radius * 4.0) / (spot * spot);
                light += color * albedo * lasers.params.z * shape * max(dot(N, -d), 0.0) * transmittance(hit);
            }
        }
    }

    for (int j = 0; j < LASER_PROJECTORS; ++j) {
        Projector p = laserProjector(j);
        if (p.pattern != 4 || dot(p.color, p.color) <= 0.0) continue;
        vec3 centre, normal;
        laserSheet(p, lasers.params.x, centre, normal);
        // Where the view ray crosses the sheet.
        float facing = dot(v, normal);
        if (abs(facing) > 1e-4) {
            float t = dot(p.origin - eye, normal) / facing;
            if (t > 0.0 && t < reach) {
                vec3 q = eye + v * t;
                vec3 out_ = q - p.origin;
                float r = length(out_);
                float angle = acos(clamp(dot(out_ / max(r, 1e-4), centre), -1.0, 1.0));
                float edge = 1.0 - smoothstep(p.spread * 0.97, p.spread, angle);
                if (edge > 0.0 && r > 0.05) {
                    float sigma = density(q);
                    if (sigma > 0.0 && !blocked(q, p.origin)) {
                        light += p.color * sigma * phaseHG(dot(out_ / r, -v), g) * edge
                               / (2.0 * p.spread * r * max(abs(facing), 0.05)) * transmittance(q);
                    }
                }
            }
        }
        // Its line across this pixel's surface.
        if (surface) {
            vec3 out_ = surfacePos - p.origin;
            float r = length(out_);
            float off = abs(dot(out_, normal));
            float line = max(radius * 2.0, pixelAngle * reach * 0.8);
            float angle = acos(clamp(dot(out_ / max(r, 1e-4), centre), -1.0, 1.0));
            if (off < line * 4.0 && angle < p.spread && r > 0.05) {
                if (!blocked(surfacePos + N * 0.01, p.origin)) {
                    float shape = exp(-0.5 * off * off / (line * line)) * (radius * 2.0) / line;
                    light += p.color * albedo * lasers.params.z * 0.02 * shape / (2.0 * p.spread * max(r, 0.5))
                           * max(dot(N, -out_ / r), 0.0) * transmittance(surfacePos);
                }
            }
        }
    }
    outColor = vec4(light, 0.0);
}
