#version 450
//
// The terrain's fragment shader. With a chunk colour whose red is below 0,
// the planet's ground is coloured by height, slope, latitude and a
// moisture noise, and the moon's grey (moonColour), both detailed by the
// ground's textures (ground.glsl); otherwise it takes the chunk's colour,
// with face lines (shading.glsl). The planet is lit by the sunlight that
// comes through the atmosphere and a sky light (atmosphere.glsl), the moon
// by the sunlight alone, in HDR.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "shading.glsl"
#include "shadow.glsl"
#include "clouds.glsl"
#define GROUND_SET 2
#include "ground.glsl"

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

// The planet's ground as weights of the layers (ground.glsl) and their
// colours: the sea floor (on the sand's texture) darker with depth; sand
// below 40 m; dry ground, grass or forest by a moisture noise; rock where
// the slope is steep; snow above a snow line that falls from 2,500 m at the
// equator to 200 m at the poles.
void groundLayers(vec3 radial, float height, vec3 normal, out float weights[GROUND_LAYERS],
                  out vec3 colours[GROUND_LAYERS]) {
    for (int i = 0; i < GROUND_LAYERS; ++i) weights[i] = 0.0;
    colours = vec3[GROUND_LAYERS](SAND, DRY, GRASS, FOREST, ROCK, SNOW, vec3(0.0));
    if (height < 0.0) {
        weights[GROUND_SAND] = 1.0;
        colours[GROUND_SAND] = mix(SEABED_SHALLOW, SEABED_DEEP, smoothstep(0.0, 800.0, -height));
        return;
    }
    float slope = 1.0 - dot(normal, radial);                  // 0 flat, 1 vertical
    float latitude = abs(radial.y);
    float moisture = fbm3(radial * 9.0 + terrain.seed.xyz + vec3(3.1, 0.7, 5.3), 4, 2.0, 0.5) * 2.0 + 0.5;
    float green = smoothstep(0.3, 0.5, moisture);
    float forest = smoothstep(0.55, 0.8, moisture);
    float land = smoothstep(10.0, 40.0, height);
    float rock = smoothstep(0.12, 0.22, slope);
    float snowLine = mix(2500.0, 200.0, latitude * latitude);
    float snow = smoothstep(snowLine - 150.0, snowLine + 150.0, height) * (1.0 - smoothstep(0.35, 0.5, slope));
    float open = (1.0 - rock) * (1.0 - snow);
    weights[GROUND_DRY] = (1.0 - green) * land * open;
    weights[GROUND_GRASS] = green * (1.0 - forest) * land * open;
    weights[GROUND_FOREST] = green * forest * land * open;
    weights[GROUND_SAND] = (1.0 - land) * open;
    weights[GROUND_ROCK] = rock * (1.0 - snow);
    weights[GROUND_SNOW] = snow;
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
    float weights[GROUND_LAYERS];
    vec3 colours[GROUND_LAYERS];
    vec3 albedo, shaded = normal;
    if (fragColour.r >= 0.0) {
        albedo = withGrid(fragColour.rgb, fragColour.a, fragFaceUv);
    } else if (fragBody == MOON) {
        for (int i = 0; i < GROUND_LAYERS; ++i) {
            weights[i] = 0.0;
            colours[i] = vec3(0.0);
        }
        weights[GROUND_MOON] = 1.0;
        colours[GROUND_MOON] = moonColour(radial, normal);
        groundTextured(weights, colours, fragRelativePos, fragBody, normal, albedo, shaded);
    } else {
        groundLayers(radial, fragRadialHeight.w, normal, weights, colours);
        groundTextured(weights, colours, fragRelativePos, fragBody, normal, albedo, shaded);
    }
    if (fragBody == MOON) {
        // No air: the sunlight alone (sunlightAt is 0 in the planet's shadow), no sky light, no clouds.
        float sunCos = max(dot(shaded, atmosphere.sun.xyz), 0.0);
        outColor = vec4(albedo / ATMOSPHERE_PI * sunlightAt(p) * sunCos * terrainShadow, 1.0);
        return;
    }
    outColor = vec4(lightSurface(albedo, p, shaded, radial, terrainShadow * cloudShadow(p)), 1.0);
}
