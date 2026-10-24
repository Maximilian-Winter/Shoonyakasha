#version 450
//
// The ocean's surface, blended over the sea floor already drawn
// (premultiplied: the colour written is added, and the sea floor is kept by
// one minus the alpha written). Light reflected by the surface (Schlick
// Fresnel, water's F0 of 0.02) shows the sky and the sun; light that enters
// shows the water's colour, more of it the more water the view crosses
// down to the sea floor. Small ripples, moving with time, tilt the normal;
// they fade out with distance.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"

layout(location = 0) in vec3 fragRadial;
layout(location = 1) in float fragWaterDepth;
layout(location = 2) in vec3 fragRelativePos;

layout(location = 0) out vec4 outColor;

const vec3 WATER = vec3(0.010, 0.045, 0.060);   // albedo of the light scattered back out of the water
const float CLEARNESS = 12.0;                    // metres of water that let through 1/e of the sea floor
const float RIPPLE_SIZE = 20.0;                  // metres
const float RIPPLE_TILT = 0.08;
const float RIPPLE_FADE = 3000.0;                // metres from the camera where the ripples have faded
const float SHININESS = 400.0;

vec3 rippledNormal(vec3 p, vec3 radial, float distance) {
    vec3 axis = abs(radial.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 t1 = normalize(cross(axis, radial));
    vec3 t2 = cross(radial, t1);
    vec3 q = p / RIPPLE_SIZE + vec3(0.0, camera.time * 0.4, 0.0);
    vec2 slope = vec2(noise3(q), noise3(q * 1.7 + vec3(4.3, 1.9, 7.7)));
    float strength = RIPPLE_TILT * (1.0 - smoothstep(0.0, RIPPLE_FADE, distance));
    return normalize(radial + strength * (slope.x * t1 + slope.y * t2));
}

void main() {
    vec3 p = camera.cameraFromCentre.xyz + fragRelativePos;
    vec3 radial = normalize(fragRadial);
    float distance = length(fragRelativePos);
    vec3 toCamera = -fragRelativePos / max(distance, 1e-3);
    vec3 normal = rippledNormal(p, radial, distance);
    vec3 sunDir = atmosphere.sun.xyz;

    float cosView = max(dot(normal, toCamera), 0.02);
    float fresnel = 0.02 + 0.98 * pow(1.0 - cosView, 5.0);

    vec3 sunlight = sunlightAt(p);
    vec3 skylight = skylightAt(p, radial);

    // Light that enters the water, scattered back out by it, and how much of
    // the sea floor shows through the water the view crosses.
    float path = max(fragWaterDepth, 0.0) / max(dot(radial, toCamera), 0.05);
    float opacity = 1.0 - exp(-path / CLEARNESS);
    vec3 body = WATER / ATMOSPHERE_PI * (sunlight * max(dot(radial, sunDir), 0.0) + skylight);

    // Light reflected by the surface: the sky's (its radiance, taken as the
    // sky light over pi) and the sun's highlight (normalised Blinn-Phong).
    vec3 sky = skylight / ATMOSPHERE_PI;
    vec3 halfway = normalize(toCamera + sunDir);
    float highlight = pow(max(dot(normal, halfway), 0.0), SHININESS) * (SHININESS + 8.0) / (8.0 * ATMOSPHERE_PI);
    vec3 sun = sunlight * highlight * fresnel * step(0.0, dot(normal, sunDir));

    vec3 colour = fresnel * sky + sun + (1.0 - fresnel) * opacity * body;
    float alpha = 1.0 - (1.0 - fresnel) * (1.0 - opacity);
    outColor = vec4(colour, alpha);
}
