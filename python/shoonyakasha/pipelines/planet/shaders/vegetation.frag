#version 450
//
// The vegetation's fragment shader, in HDR, with the sun's shadow map and
// the clouds' shadows. Each part's albedo:
//
//     bark      the bark tile times the shade
//     card      the foliage atlas (each card's mean is CARD_MEAN, so it is
//     grass     times 2) times the plant's colour and the shade; a texel
//               whose coverage is below 1/2 is not drawn (foliage.glsl)
//     impostor  the impostor's texel, times 2 and the plant's colour where
//               its normal's alpha marks foliage; its normal from the
//               impostor's normals in the quad's frame
//     plain     the plant's colour times the shade
//
// Bark and plain parts are lit as diffuse surfaces (atmosphere.glsl's
// lightSurface), a back face with its normal turned round. Foliage is lit
// by the sun wrapped FOLIAGE_WRAP past its terminator, light through it
// where the sun is behind it (FOLIAGE_THROUGH of the sunlight on its
// back), a glow towards the sun seen through it (FOLIAGE_GLOW), all of
// which still reach it in shadow by FOLIAGE_LEAK, and the sky light times
// FOLIAGE_SKY, darkened by the shade.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "shadow.glsl"
#include "clouds.glsl"
#define FOLIAGE_SET 2
#include "foliage.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColour;
layout(location = 2) in vec3 fragRelativePos;
layout(location = 3) flat in int fragPart;
layout(location = 4) in vec2 fragUv;
layout(location = 5) in float fragShade;
layout(location = 6) flat in vec3 fragRight;
layout(location = 7) flat in vec3 fragUp;
layout(location = 8) flat in vec3 fragFacing;

layout(location = 0) out vec4 outColor;

const int PART_BARK = 0;
const int PART_CARD = 1;
const int PART_GRASS = 2;
const int PART_IMPOSTOR = 3;
const float FOLIAGE_WRAP = 0.5;
const float FOLIAGE_THROUGH = 0.45;
const float FOLIAGE_GLOW = 0.6;
const float FOLIAGE_LEAK = 0.25;    // of the sunlight: what reaches foliage in shadow through the leaves
const float FOLIAGE_SKY = 1.6;      // the sky light on foliage, of a level surface's

void main() {
    vec3 p = camera.cameraFromCentre.xyz + fragRelativePos;
    vec3 radial = normalize(p);
    vec3 normal = normalize(fragNormal);
    vec3 albedo;
    bool foliage = true;
    bool drawn = true;
    if (fragPart == PART_BARK) {
        albedo = texture(vegetationBark, fragUv).rgb * fragShade;
        foliage = false;
    } else if (fragPart == PART_CARD || fragPart == PART_GRASS) {
        vec4 texel = foliageTexel(vegetationAtlas, fragUv, drawn);
        albedo = 2.0 * texel.rgb * fragColour * fragShade;
    } else if (fragPart == PART_IMPOSTOR) {
        vec4 texel = foliageTexel(vegetationImpostor, fragUv, drawn);
        vec4 bumps = texture(vegetationImpostorNormal, fragUv);
        vec3 n = bumps.xyz * 2.0 - 1.0;
        normal = normalize(fragRight * n.x + fragUp * n.y + fragFacing * n.z);
        foliage = bumps.a > 0.5;
        albedo = foliage ? 2.0 * texel.rgb * fragColour : texel.rgb;
    } else {
        albedo = fragColour * fragShade;
        foliage = false;
    }
    if (!drawn) discard;

    float sunShare = sunShadow(fragRelativePos, normal) * cloudShadow(p);
    if (!foliage) {
        if (!gl_FrontFacing && fragPart != PART_IMPOSTOR) normal = -normal;
        outColor = vec4(lightSurface(albedo, p, normal, radial, sunShare), 1.0);
        return;
    }
    vec3 sun = atmosphere.sun.xyz;
    float facing = dot(normal, sun);
    float lit = max((facing + FOLIAGE_WRAP) / (1.0 + FOLIAGE_WRAP), 0.0);
    float through = FOLIAGE_THROUGH * max(-facing, 0.0);
    float glow = FOLIAGE_GLOW * pow(max(dot(normalize(fragRelativePos), sun), 0.0), 8.0);
    float skyCos = FOLIAGE_SKY * (0.6 + 0.4 * dot(normal, radial)) * fragShade;
    float sunReach = mix(FOLIAGE_LEAK, 1.0, sunShare);
    vec3 light = sunlightAt(p) * sunReach * (lit + through + glow) + skylightAt(p, radial) * skyCos;
    outColor = vec4(albedo / ATMOSPHERE_PI * light, 1.0);
}
