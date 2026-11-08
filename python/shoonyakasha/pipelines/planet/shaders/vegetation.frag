#version 450
//
// The vegetation's fragment shader, in HDR, with the sun's shadow map and
// the clouds' shadows. Each part's albedo:
//
//     bark      the bark tile times the shade
//     card      the foliage atlas (each card's mean is CARD_MEAN, so it is
//     grass     times 2) times the plant's colour and the shade; a texel
//               is drawn by its coverage (foliage.glsl's, a card's and a
//               cut-out scan's times its edge fade) as foliageDrawn tells
//     impostor  its tree's octahedral views blended (foliage.glsl's
//               impostorTexel), times 2 and the plant's colour where its
//               normal's alpha marks foliage; its normal from the views'
//               normals in the tree's frame
//     plain     the plant's colour times the shade; a rock's (fragRock)
//               also times its grain (rockGrain): ROCK_GRAIN of a noise on
//               its mesh's own position over three octaves, the finest
//               ROCK_GRAIN_SCALE across a unit of its height, which bumps
//               its normal by ROCK_BUMP as well
//     scan      its material's texel in the scans' atlas (foliage.glsl's
//               scanTexel) times the plant's colour, not drawn where a
//               cut-out material's coverage is below 1/2; its normal from
//               its normal map in its tangent frame. A cut-out material is
//               lit as foliage, any other as a diffuse surface.
//
// Bark and plain parts are lit as diffuse surfaces (atmosphere.glsl's
// lightSurface), a back face with its normal turned round; a rock on the
// moon by the sunlight alone, as the moon's ground. An ore rock adds its
// glowing specks (oreSpeck) in its resource's colour (ore_colours.glsl),
// lit or not.
//
// foliageDrawn: the coverage sharpened about FOLIAGE_CUTOFF to about a
// pixel's width (over its rate of change across the screen) and, with
// temporal anti-aliasing, compared with a noise on the pixel that moves
// with each frame's jitter, so the resolved edges are soft; without, the
// coverage reaching FOLIAGE_CUTOFF. A card's and a cut-out scan's coverage
// fades from FOLIAGE_EDGE_FADE.y of its face's cosine to the view down to
// FOLIAGE_EDGE_FADE.x, so it thins out seen edge-on.
//
// Foliage is lit by the sun wrapped FOLIAGE_WRAP past its terminator, light
// through it where the sun is behind it (FOLIAGE_THROUGH of the sunlight
// on its back) and a glow towards the sun seen through it (FOLIAGE_GLOW,
// the cosine to the sun to the FOLIAGE_GLOW_POWER), the light through it
// coloured by FOLIAGE_TRANSMIT; all of these still reach it in shadow by
// FOLIAGE_LEAK times the shade. It takes the sky light times FOLIAGE_SKY
// and the ground's light from below (atmosphere.glsl's groundLightAt),
// both darkened by the shade, and a sheen: FOLIAGE_SHEEN of the sunlight
// reflected (normalised Blinn-Phong to FOLIAGE_SHEEN_POWER). A card's
// normal is FOLIAGE_FACE of its face's normal (turned to the crown's side)
// and the rest its crown's. Every part also takes the local lights
// (lights.glsl), foliage from either side.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "lights.glsl"
#include "shadow.glsl"
#include "clouds.glsl"
#define FOLIAGE_SET 2
#include "foliage.glsl"
#include "ore_colours.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColour;
layout(location = 2) in vec3 fragRelativePos;
layout(location = 3) flat in int fragPart;
layout(location = 4) in vec2 fragUv;
layout(location = 5) in float fragShade;
layout(location = 6) flat in vec3 fragRight;
layout(location = 7) flat in vec3 fragUp;
layout(location = 8) flat in vec3 fragFacing;
layout(location = 9) flat in int fragOnMoon;
layout(location = 10) in vec3 fragLocal;
layout(location = 11) flat in int fragResource;
layout(location = 12) in vec3 fragTangent;
layout(location = 13) flat in vec4 fragCell;
layout(location = 14) flat in int fragRock;

layout(location = 0) out vec4 outColor;

const int PART_BARK = 0;
const int PART_CARD = 1;
const int PART_GRASS = 2;
const int PART_IMPOSTOR = 3;
const int PART_SCAN = 5;
const float FOLIAGE_WRAP = 0.5;
const float FOLIAGE_THROUGH = 0.5;
const float FOLIAGE_GLOW = 0.6;
const float FOLIAGE_GLOW_POWER = 6.0;
const vec3 FOLIAGE_TRANSMIT = vec3(1.1, 1.05, 0.35);   // the light through a leaf, of its albedo
const float FOLIAGE_LEAK = 0.18;    // of the sunlight: what reaches foliage in shadow through the leaves
const float FOLIAGE_SHEEN = 0.006;
const float FOLIAGE_SHEEN_POWER = 48.0;
const float FOLIAGE_FACE = 0.4;     // of a card's normal: its face's, the rest its crown's
const vec2 FOLIAGE_EDGE_FADE = vec2(0.15, 0.35);  // a face's cosine to the view: coverage none .. all
const float FOLIAGE_SKY = 1.4;      // the sky light on foliage, of a level surface's
const float ORE_SPECK_CELLS = 24.0; // cells per unit of the mesh (its height) the specks are hashed in
const float ORE_SPECK_SHARE = 0.12; // of the cells that hold a speck
const float ORE_SPECK_RADIUS = 0.3; // of a cell
const float ORE_SPECK_GLOW = 0.2;   // of the sun's illuminance over pi
const float ROCK_GRAIN = 0.6;
const float ROCK_GRAIN_SCALE = 12.0;
const float ROCK_BUMP = 0.6;

// A rock's grain at `local` (its mesh's position): about -1 .. 1.
float rockGrain(vec3 local) {
    vec3 q = local * ROCK_GRAIN_SCALE;
    return 0.25 * noise3(q) + 0.35 * noise3(q * 0.37 + 17.3) + 0.4 * noise3(q * 0.13 + 41.7);
}

// `normal` tilted by the screen-space slope of `bump` over the surface at `position`.
vec3 bumped(vec3 normal, vec3 position, float bump) {
    vec3 dx = dFdx(position), dy = dFdy(position);
    vec3 r1 = cross(dy, normal), r2 = cross(normal, dx);
    float det = dot(dx, r1);
    if (abs(det) < 1e-12) return normal;
    vec3 gradient = sign(det) * (dFdx(bump) * r1 + dFdy(bump) * r2);
    return normalize(abs(det) * normal - gradient);
}

// An ore rock's glowing specks: 1 inside a speck, 0 elsewhere. A speck is a
// sphere of ORE_SPECK_RADIUS of a cell around a hashed point of the cells
// of the mesh's own position that ORE_SPECK_SHARE of them hold.
float oreSpeck(vec3 local) {
    vec3 q = local * ORE_SPECK_CELLS;
    ivec3 cell = ivec3(floor(q));
    uint h = terrainHash(cell + ivec3(911, 353, 727));
    if (float(h & 0xFFFFu) / 65536.0 >= ORE_SPECK_SHARE) return 0.0;
    vec3 centre = vec3(cell) + 0.25 + 0.5 * vec3(float((h >> 16) & 0xFu), float((h >> 20) & 0xFu),
                                                 float((h >> 24) & 0xFu)) / 15.0;
    return 1.0 - smoothstep(ORE_SPECK_RADIUS * 0.6, ORE_SPECK_RADIUS, length(q - centre));
}

// Whether a foliage texel of `coverage` is drawn.
bool foliageDrawn(float coverage) {
    if (camera.taa.z < 0.5) return coverage >= FOLIAGE_CUTOFF;
    float sharp = clamp((coverage - FOLIAGE_CUTOFF) / max(fwidth(coverage), 1e-4) + 0.5, 0.0, 1.0);
    vec2 at = gl_FragCoord.xy + floor(fract(camera.taa.xy * 4096.0) * 64.0);
    float noise = fract(52.9829189 * fract(dot(at, vec2(0.06711056, 0.00583715))));
    return sharp > noise;
}

void main() {
    vec3 p = camera.cameraFromCentre.xyz + fragRelativePos;
    vec3 radial = normalize(p);
    vec3 normal = normalize(fragNormal);
    vec3 face = cross(dFdx(fragRelativePos), dFdy(fragRelativePos));
    face = dot(face, face) > 1e-20 ? normalize(face) : normal;
    vec3 toEye = -normalize(fragRelativePos);
    float edgeFade = smoothstep(FOLIAGE_EDGE_FADE.x, FOLIAGE_EDGE_FADE.y, abs(dot(toEye, face)));
    vec3 albedo;
    bool foliage = true;
    bool drawn = true;
    if (fragPart == PART_BARK) {
        albedo = texture(vegetationBark, fragUv).rgb * fragShade;
        foliage = false;
    } else if (fragPart == PART_CARD || fragPart == PART_GRASS) {
        vec4 texel = foliageTexel(vegetationAtlas, fragUv, drawn);
        drawn = foliageDrawn(texel.a * (fragPart == PART_CARD ? edgeFade : 1.0));
        albedo = 2.0 * texel.rgb * fragColour * fragShade;
        if (fragPart == PART_CARD) {
            normal = normalize(mix(normal, dot(face, normal) < 0.0 ? -face : face, FOLIAGE_FACE));
        }
    } else if (fragPart == PART_IMPOSTOR) {
        vec4 texel, bumps;
        impostorTexel(fragLocal, fragTangent, fragUv, false, texel, bumps, drawn);
        vec3 n = bumps.xyz * 2.0 - 1.0;
        normal = normalize(fragRight * n.x + fragUp * n.y + fragFacing * n.z);
        foliage = bumps.a > 0.5;
        albedo = foliage ? 2.0 * texel.rgb * fragColour : texel.rgb;
    } else if (fragPart >= PART_SCAN) {
        int code = fragPart - PART_SCAN;
        bool cutout = (code & 2) != 0;
        float sign = (code & 1) != 0 ? -1.0 : 1.0;
        vec4 texel = scanTexel(scanAlbedo, fragCell, fragUv);
        drawn = !cutout || foliageDrawn(texel.a * edgeFade);
        vec3 n = scanTexel(scanNormal, fragCell, fragUv).xyz * 2.0 - 1.0;
        if (!gl_FrontFacing) normal = -normal;
        vec3 t = fragTangent - normal * dot(fragTangent, normal);
        if (dot(t, t) > 1e-8) {
            t = normalize(t);
            normal = normalize(t * n.x + sign * cross(normal, t) * n.y + normal * n.z);
        }
        albedo = texel.rgb * fragColour;
        foliage = cutout;
    } else {
        albedo = fragColour * fragShade;
        foliage = false;
        if (fragRock == 1) {
            float grain = rockGrain(fragLocal);
            albedo *= 1.0 + ROCK_GRAIN * grain;
            normal = bumped(normal, fragRelativePos, ROCK_BUMP * grain * length(dFdx(fragRelativePos)) /
                            max(length(dFdx(fragLocal)), 1e-6) / ROCK_GRAIN_SCALE);
        }
    }
    if (!drawn) discard;

    float sunShare = sunShadow(fragRelativePos, normal) * cloudShadow(p);
    if (!foliage) {
        if (!gl_FrontFacing && fragPart != PART_IMPOSTOR && fragPart < PART_SCAN) normal = -normal;
        vec3 glow = fragResource > 0
                  ? oreSpeckColour(uint(fragResource)) * (ORE_SPECK_GLOW * atmosphere.mie.z / ATMOSPHERE_PI
                                                          * oreSpeck(fragLocal))
                  : vec3(0.0);
        if (fragOnMoon == 1) {
            // No air: the sunlight alone, as on the moon's ground.
            float sunCos = max(dot(normal, atmosphere.sun.xyz), 0.0);
            outColor = vec4(albedo / ATMOSPHERE_PI * sunlightAt(p) * sunCos * sunShadow(fragRelativePos, normal)
                            + localLights(albedo, fragRelativePos, normal, false) + glow, 1.0);
            return;
        }
        outColor = vec4(lightSurface(albedo, p, normal, radial, sunShare)
                        + localLights(albedo, fragRelativePos, normal, false) + glow, 1.0);
        return;
    }
    vec3 sun = atmosphere.sun.xyz;
    float facing = dot(normal, sun);
    float lit = max((facing + FOLIAGE_WRAP) / (1.0 + FOLIAGE_WRAP), 0.0);
    float through = FOLIAGE_THROUGH * max(-facing, 0.0);
    float glow = FOLIAGE_GLOW * pow(max(dot(-toEye, sun), 0.0), FOLIAGE_GLOW_POWER);
    float skyCos = FOLIAGE_SKY * (0.6 + 0.4 * dot(normal, radial)) * fragShade;
    float sunReach = mix(FOLIAGE_LEAK * fragShade, 1.0, sunShare);
    float groundCos = (0.4 - 0.4 * dot(normal, radial)) * fragShade;
    vec3 sunlight = sunlightAt(p);
    vec3 light = sunlight * sunReach * (vec3(lit) + (through + glow) * FOLIAGE_TRANSMIT)
               + skylightAt(p, radial) * skyCos + groundLightAt(p, radial) * groundCos;
    vec3 seen = dot(normal, toEye) < 0.0 ? -normal : normal;
    float sheen = FOLIAGE_SHEEN * (FOLIAGE_SHEEN_POWER + 8.0) / (8.0 * ATMOSPHERE_PI)
                * pow(max(dot(seen, normalize(sun + toEye)), 0.0), FOLIAGE_SHEEN_POWER) * max(dot(seen, sun), 0.0);
    outColor = vec4(albedo / ATMOSPHERE_PI * light + sunlight * sunShare * sheen
                    + localLights(albedo, fragRelativePos, normal, true), 1.0);
}
