#version 450
//
// The terrain's fragment shader. With a chunk colour whose red is below 0,
// the ground is coloured by height, slope, latitude and a moisture noise;
// otherwise it takes the chunk's colour. Shaded as in shading.glsl.
//

#include "shading.glsl"

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;
} camera;

layout(set = 0, binding = 1) uniform TerrainParams {
    vec4 continent;
    vec4 mountains;
    vec4 hills;
    vec4 seed;
    vec4 site;
    vec4 siteShape;
} terrain;

#include "terrain.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec4 fragColour;
layout(location = 2) in vec2 fragFaceUv;
layout(location = 3) in vec4 fragRadialHeight;

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

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 radial = normalize(fragRadialHeight.xyz);
    vec3 albedo = fragColour.r < 0.0 ? groundColour(radial, fragRadialHeight.w, normal) : fragColour.rgb;
    vec3 lit = shade(albedo, fragColour.a, fragFaceUv, normal, camera.sunDirection.xyz);
    // The swapchain is sRGB: writing to it encodes the linear colour.
    outColor = vec4(lit, 1.0);
}
