// A water surface's colour, for the ocean and the lakes (ocean.frag,
// lake.frag), premultiplied: the colour is added and what lies under the
// water is kept by one minus the alpha. Light reflected by the surface
// (Schlick Fresnel, water's F0 of 0.02) shows the sky and the sun, the sky
// as the sky probe's light along the reflected view (sky_probe.glsl),
// giving way to the sky light over pi as the camera climbs from
// PROBE_HEIGHTS.x to PROBE_HEIGHTS.y above the ground (the probe sees the
// sky from the camera); light
// that enters shows the water's colour, more of it the more water the view
// crosses down to the ground under it. Small ripples, moving with time, tilt
// the normal; they fade out with distance, and the sun's highlight widens
// instead (its shininess falls from SHININESS to FAR_SHININESS between
// RIPPLE_FADE and FAR_DISTANCE), standing for waves too small to see.
//
// Include after camera.glsl, terrain.glsl, atmosphere.glsl, shadow.glsl and clouds.glsl.

#include "sky_probe.glsl"

const vec3 WATER = vec3(0.010, 0.045, 0.060);   // albedo of the light scattered back out of the water
const float CLEARNESS = 12.0;                    // metres of water that let through 1/e of the ground under it
const float RIPPLE_SIZE = 20.0;                  // metres
const float RIPPLE_TILT = 0.08;
const float RIPPLE_FADE = 3000.0;                // metres from the camera where the ripples have faded
const float SHININESS = 400.0;
const float FAR_SHININESS = 30.0;
const float FAR_DISTANCE = 20000.0;              // metres
const vec2 PROBE_HEIGHTS = vec2(2000.0, 10000.0);  // metres

vec3 rippledNormal(vec3 p, vec3 radial, float distance) {
    vec3 axis = abs(radial.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 t1 = normalize(cross(axis, radial));
    vec3 t2 = cross(radial, t1);
    vec3 q = p / RIPPLE_SIZE + vec3(0.0, camera.time * 0.4, 0.0);
    vec2 slope = vec2(noise3(q), noise3(q * 1.7 + vec3(4.3, 1.9, 7.7)));
    float strength = RIPPLE_TILT * (1.0 - smoothstep(0.0, RIPPLE_FADE, distance));
    return normalize(radial + strength * (slope.x * t1 + slope.y * t2));
}

// The surface at `relativePos` (from the camera) whose up is the unit
// `radial`, with the sine of its terrain's horizon towards the sun
// (`horizon`) and `waterDepth` metres of water under it.
vec4 waterSurface(vec3 relativePos, vec3 radial, float horizon, float waterDepth) {
    vec3 p = camera.cameraFromCentre.xyz + relativePos;
    float distance = length(relativePos);
    vec3 toCamera = -relativePos / max(distance, 1e-3);
    vec3 normal = rippledNormal(p, radial, distance);
    vec3 sunDir = atmosphere.sun.xyz;

    float cosView = max(dot(normal, toCamera), 0.02);
    float fresnel = 0.02 + 0.98 * pow(1.0 - cosView, 5.0);

    vec3 sunlight = sunlightAt(p) * min(sunShadow(relativePos, radial), horizonShadow(horizon, radial, distance))
                  * cloudShadow(p);
    vec3 skylight = skylightAt(p, radial);

    // Light that enters the water, scattered back out by it, and how much of
    // the ground under it shows through the water the view crosses.
    float path = waterDepth / max(dot(radial, toCamera), 0.05);
    float opacity = 1.0 - exp(-path / CLEARNESS);
    vec3 body = WATER / ATMOSPHERE_PI * (sunlight * max(dot(radial, sunDir), 0.0) + skylight);

    // Light reflected by the surface: the sky's and the sun's highlight
    // (normalised Blinn-Phong).
    float cameraHeight = length(camera.cameraFromCentre.xyz) - atmosphere.radii.x;
    vec3 sky = mix(skyReflection(reflect(-toCamera, normal), 0.0), skylight / ATMOSPHERE_PI,
                   smoothstep(PROBE_HEIGHTS.x, PROBE_HEIGHTS.y, cameraHeight));
    vec3 halfway = normalize(toCamera + sunDir);
    // Geometric between the near and far shininess, so the widening is even in the exponent.
    float shininess = SHININESS * pow(FAR_SHININESS / SHININESS, smoothstep(RIPPLE_FADE, FAR_DISTANCE, distance));
    float highlight = pow(max(dot(normal, halfway), 0.0), shininess) * (shininess + 8.0) / (8.0 * ATMOSPHERE_PI);
    vec3 sun = sunlight * highlight * fresnel * step(0.0, dot(normal, sunDir));

    vec3 colour = fresnel * sky + sun + (1.0 - fresnel) * opacity * body;
    float alpha = 1.0 - (1.0 - fresnel) * (1.0 - opacity);
    return vec4(colour, alpha);
}
