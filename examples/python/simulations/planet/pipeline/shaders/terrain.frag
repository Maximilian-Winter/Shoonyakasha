#version 450
//
// The terrain's fragment shader. With a chunk colour whose red is below 0,
// the planet's ground is coloured by height, slope, latitude and a
// moisture noise, and the moon's grey (moonColour); otherwise it takes the
// chunk's colour, with face lines (shading.glsl). The planet is lit by the
// sunlight that comes through the atmosphere and a sky light
// (atmosphere.glsl), the moon by the sunlight alone, in HDR.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "shading.glsl"
#include "shadow.glsl"
#include "clouds.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec4 fragColour;
layout(location = 2) in vec2 fragFaceUv;
layout(location = 3) in vec4 fragRadialHeight;
layout(location = 4) in vec3 fragRelativePos;
layout(location = 5) in float fragHorizon;
layout(location = 6) flat in int fragBody;

layout(location = 0) out vec4 outColor;

const vec3 SEABED_DEEP = vec3(0.02, 0.05, 0.12);
const vec3 SEABED_SHALLOW = vec3(0.10, 0.22, 0.30);
const vec3 SAND = vec3(0.62, 0.55, 0.38);
const vec3 GRASS = vec3(0.16, 0.30, 0.08);
const vec3 FOREST = vec3(0.06, 0.16, 0.05);
const vec3 DRY = vec3(0.45, 0.38, 0.22);
const vec3 ROCK = vec3(0.30, 0.27, 0.25);
const vec3 SNOW = vec3(0.85, 0.87, 0.90);

vec3 groundColour(vec3 radial, float height, vec3 normal) {
    float slope = 1.0 - dot(normal, radial);                  // 0 flat, 1 vertical
    float latitude = abs(radial.y);
    if (height < 0.0) {
        return mix(SEABED_SHALLOW, SEABED_DEEP, smoothstep(0.0, 800.0, -height));
    }
    float moisture = fbm3(radial * 9.0 + terrain.seed.xyz + vec3(3.1, 0.7, 5.3), 4, 2.0, 0.5) * 2.0 + 0.5;
    vec3 colour = mix(DRY, mix(GRASS, FOREST, smoothstep(0.55, 0.8, moisture)), smoothstep(0.3, 0.5, moisture));
    colour = mix(SAND, colour, smoothstep(10.0, 40.0, height));
    colour = mix(colour, ROCK, smoothstep(0.12, 0.22, slope));
    float snowLine = mix(2500.0, 200.0, latitude * latitude);
    float snow = smoothstep(snowLine - 150.0, snowLine + 150.0, height) * (1.0 - smoothstep(0.35, 0.5, slope));
    return mix(colour, SNOW, snow);
}

// The moon's ground: grey highlands and darker maria, from a low-frequency
// noise, varied by a finer one, lighter on steep slopes.
const vec3 MOON_HIGHLAND = vec3(0.17, 0.165, 0.155);
const vec3 MOON_MARE = vec3(0.075, 0.073, 0.07);

vec3 moonColour(vec3 radial, vec3 normal) {
    float slope = 1.0 - dot(normal, radial);
    float mare = smoothstep(0.05, 0.2, fbm3(radial * 1.5 + vec3(11.7, 3.3, 8.9), 4, 2.0, 0.5));
    vec3 colour = mix(MOON_HIGHLAND, MOON_MARE, mare);
    colour *= 0.85 + 0.3 * (fbm3(radial * 60.0 + vec3(2.9, 5.1, 7.7), 4, 2.0, 0.5) + 0.5);
    return colour + 0.05 * smoothstep(0.15, 0.4, slope);
}

const int MOON = 1;          // the moon's body index, as chunk_build.comp

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 radial = normalize(fragRadialHeight.xyz);
    vec3 p = camera.cameraFromCentre.xyz + fragRelativePos;
    float terrainShadow = min(sunShadow(fragRelativePos, normal),
                              horizonShadow(fragHorizon, radial, length(fragRelativePos)));
    if (fragBody == MOON) {
        // No air: the sunlight alone (sunlightAt is 0 in the planet's shadow), no sky light, no clouds.
        vec3 albedo = fragColour.r < 0.0 ? moonColour(radial, normal)
                                         : withGrid(fragColour.rgb, fragColour.a, fragFaceUv);
        float sunCos = max(dot(normal, atmosphere.sun.xyz), 0.0);
        outColor = vec4(albedo / ATMOSPHERE_PI * sunlightAt(p) * sunCos * terrainShadow, 1.0);
        return;
    }
    vec3 albedo = fragColour.r < 0.0 ? groundColour(radial, fragRadialHeight.w, normal)
                                     : withGrid(fragColour.rgb, fragColour.a, fragFaceUv);
    outColor = vec4(lightSurface(albedo, p, normal, radial, terrainShadow * cloudShadow(p)), 1.0);
}
