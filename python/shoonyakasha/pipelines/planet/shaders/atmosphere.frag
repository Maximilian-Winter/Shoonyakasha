#version 450
//
// The atmosphere over the scene, then exposure and tone mapping. For each
// pixel: the view ray from the camera, the distance to what the depth
// buffer holds there (reverse-Z: 0 is the sky), the scene seen through the
// atmosphere plus the light it scatters towards the camera
// (atmosphere.glsl), and the sun's disk and the stars where the ray
// reaches the sky; then the clouds over it, and a storm's haze.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "atmosphere.glsl"
#include "sk/tonemap.glsl"

layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D sceneDepth;
layout(set = 2, binding = 0) uniform sampler2D cloudLayer;      // rgb: the clouds' light; a: their transmittance
layout(set = 2, binding = 1) uniform sampler2D cloudDistance;   // x: their mean distance (m); y: the scene's distance

// The clouds at this pixel from their lower-resolution images: the 4 x 4
// nearest texels, weighted by a cubic B-spline (so features a few texels
// across come out round, not square) and by how close the scene distance
// each was marched to is to this pixel's: a texel whose distance differs by
// CLOUD_DEPTH_TOLERANCE of it counts half, so clouds do not bleed across the
// edges of nearer things while smooth ground keeps the smooth blend.
// Returns the clouds' light and transmittance; `distance` their mean
// distance.
const float CLOUD_DEPTH_TOLERANCE = 0.05;

// The cubic B-spline's weights for the four texels around a point `f`
// (0 .. 1) past the second.
vec4 bSplineWeights(float f) {
    float f2 = f * f, f3 = f2 * f;
    return vec4(1.0 - 3.0 * f + 3.0 * f2 - f3, 4.0 - 6.0 * f2 + 3.0 * f3,
                1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3, f3) / 6.0;
}

vec4 upsampledClouds(vec2 uv, float sceneDistance, out float distance) {
    ivec2 size = textureSize(cloudLayer, 0);
    vec2 coord = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(coord)) - 1;
    vec2 f = coord - floor(coord);
    vec4 wx = bSplineWeights(f.x), wy = bSplineWeights(f.y);
    float here = min(sceneDistance, 1e9);
    vec4 cloud = vec4(0.0);
    float mean = 0.0, total = 0.0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            ivec2 texel = clamp(base + ivec2(x, y), ivec2(0), size - 1);
            vec2 d = texelFetch(cloudDistance, texel, 0).xy;
            float spline = wx[x] * wy[y];
            float difference = abs(d.y - here) / (max(here, 1.0) * CLOUD_DEPTH_TOLERANCE);
            float w = spline / (1.0 + difference * difference) + 1e-6;
            cloud += texelFetch(cloudLayer, texel, 0) * w;
            mean += d.x * w;
            total += w;
        }
    }
    distance = mean / total;
    return cloud / total;
}

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

const float SUN_COS_INNER = 0.99996;   // a disk about 1 degree across
const float SUN_COS_OUTER = 0.99990;
const float SUN_DISK = 40.0;           // the disk's brightness, times the sun's illuminance

// A storm's haze (clouds.glsl's stormShape.z): its colour, its light as a
// share of the sun's illuminance, and the distance (m) it reaches at most.
const vec3 STORM_HAZE_COLOUR = vec3(0.55, 0.58, 0.62);
const float STORM_HAZE_LIGHT = 0.05;
const float STORM_HAZE_REACH = 50000.0;

// The stars, fixed in planet space: the view direction on a cube's face
// (its largest coordinate's), that face's coordinates cut into STAR_CELLS
// cells across; STAR_SHARE of the cells hold a star at a hashed place away
// from their edges; a second hash of the cell gives its brightness, up to
// STAR_BRIGHTNESS (times the sun's illuminance), as a number 0 to 1 to the
// STAR_FALLOFF power, and its colour between blue-white and warm white.
// A star is a spot about a
// pixel across (the view direction's change across a pixel). The stars
// fade out as the sky's own light along the ray (its luminance over the
// sun's illuminance) rises from STAR_SKY_FADE.x to .y, so they show in
// the night and the late twilight only.
const vec2 STAR_SKY_FADE = vec2(0.0005, 0.005);
const float STAR_CELLS = 400.0;
const float STAR_SHARE = 0.08;
const float STAR_BRIGHTNESS = 0.01;
const float STAR_FALLOFF = 6.0;

uint starHash(ivec3 c) {
    uint h = uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

vec3 stars(vec3 rd, float pixel) {
    vec3 a = abs(rd);
    int face;
    vec2 uv;
    if (a.x >= a.y && a.x >= a.z) {
        face = rd.x > 0.0 ? 0 : 1;
        uv = rd.yz / a.x;
    } else if (a.y >= a.z) {
        face = rd.y > 0.0 ? 2 : 3;
        uv = rd.xz / a.y;
    } else {
        face = rd.z > 0.0 ? 4 : 5;
        uv = rd.xy / a.z;
    }
    vec2 q = (uv * 0.5 + 0.5) * STAR_CELLS;
    vec2 cell = floor(q);
    uint h = starHash(ivec3(ivec2(cell), face));
    if (float(h & 0xFFFFu) / 65536.0 >= STAR_SHARE) return vec3(0.0);
    vec2 centre = cell + 0.2 + 0.6 * vec2(float((h >> 16) & 0xFFu), float((h >> 24) & 0xFFu)) / 255.0;
    float d = 2.0 * length(q - centre) / STAR_CELLS;           // about radians, near the face's centre
    float spot = exp(-d * d / max(pixel * pixel, 1e-10));
    uint g = starHash(ivec3(ivec2(cell), face + 6));
    float brightness = pow(float(g & 0xFFFFu) / 65535.0, STAR_FALLOFF);
    vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.88, 0.75), float((g >> 16) & 0xFFu) / 255.0);
    return tint * (STAR_BRIGHTNESS * brightness * spot);
}

void main() {
    vec2 ndc = fragTexCoord * 2.0 - 1.0;
    float depth = texture(sceneDepth, fragTexCoord).r;
    vec4 near = camera.invProj * vec4(ndc, 1.0, 1.0);
    vec3 rd = normalize(mat3(camera.invView) * (near.xyz / near.w));

    float sceneDistance = 1e30;
    if (depth > 0.0) {
        vec4 viewPos = camera.invProj * vec4(ndc, depth, 1.0);
        sceneDistance = length(viewPos.xyz / viewPos.w);
    }

    vec3 ro = camera.cameraFromCentre.xyz;
    vec3 inscatter, transmittance;
    atmosphereAlong(ro, rd, sceneDistance, inscatter, transmittance);

    vec3 colour = texture(hdrColor, fragTexCoord).rgb;
    float pixel = length(fwidth(rd));
    if (depth == 0.0) {
        float mu = dot(rd, atmosphere.sun.xyz);
        float skyLight = dot(inscatter, vec3(0.2126, 0.7152, 0.0722)) / atmosphere.mie.z;
        float starsSeen = 1.0 - smoothstep(STAR_SKY_FADE.x, STAR_SKY_FADE.y, skyLight);
        colour = atmosphere.mie.z * (SUN_DISK * smoothstep(SUN_COS_OUTER, SUN_COS_INNER, mu) * vec3(1.0)
                                     + stars(rd, pixel) * starsSeen);
    }
    colour = colour * transmittance + inscatter;

    // The clouds (clouds.frag, at lower resolution): the air in front of
    // them, their light seen through it, and what lies behind them through
    // them.
    float cloudAt;
    vec4 cloud = upsampledClouds(fragTexCoord, sceneDistance, cloudAt);
    if (cloud.a < 0.999) {
        cloudAt = min(cloudAt, sceneDistance);
        vec3 inFront, throughFront;
        atmosphereAlong(ro, rd, cloudAt, inFront, throughFront);
        colour = inFront + throughFront * cloud.rgb + cloud.a * (colour - inFront);
    }

    // A storm's haze around the camera (clouds.stormShape.z per metre): a
    // grey lit by the sun's illuminance times STORM_HAZE_LIGHT and by the
    // sun's height, covering what lies behind it with the distance.
    float haze = clouds.stormShape.z;
    if (haze > 0.0) {
        float lit = clamp(dot(normalize(ro), atmosphere.sun.xyz) + 0.1, 0.02, 1.0);
        vec3 grey = STORM_HAZE_COLOUR * atmosphere.mie.z * STORM_HAZE_LIGHT * lit;
        colour = mix(colour, grey, 1.0 - exp(-haze * min(sceneDistance, STORM_HAZE_REACH)));
    }

    // The swapchain is sRGB: writing to it encodes the linear colour.
    outColor = vec4(ACESFilm(colour * atmosphere.mie.w), 1.0);
}
