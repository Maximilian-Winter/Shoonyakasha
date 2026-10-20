// rt_lights.glsl - shadow rays towards spot and point lights. Included by
// lights.glsl when RT_LOCAL_SHADOWS is defined. The including shader enables
// GL_EXT_ray_query, declares `accelerationStructureEXT scene`, the Camera
// block as `camera`, the Showroom block as `showroom`, and sets the two
// globals below before shading.

#ifndef SHOWROOM_RT_LIGHTS_GLSL
#define SHOWROOM_RT_LIGHTS_GLSL

// The pixel being shaded, for the noise that picks each ray's target, and
// how far off the surface rays start (more farther away, where depth is
// coarser).
vec2 rtLightPixel = vec2(0.0);
float rtLightOffset = 0.01;

// Two numbers in 0..1 for light i's ray from this pixel, new each frame.
vec2 lightRayNoise(uint i) {
    return temporalNoise2(rtLightPixel + vec2(37.0, 11.0) * float(i + 1u), camera.frame + i, camera.taa);
}

// The narrowest penumbra, in world units, of the lights whose rays were
// blocked at this pixel (the light's size times the blocker's distance from
// the surface over its distance from the light); 0 when none was.
float rtPenumbra = 0.0;

// Whether anything lies between origin and target, and if so, how far from
// origin (the first hit found, not necessarily the nearest); -1 when nothing
// does.
float segmentHit(vec3 origin, vec3 target) {
    vec3 toTarget = target - origin;
    float length_ = length(toTarget);
    if (length_ <= 1e-3) return -1.0;

    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                          0xFF, origin, 0.0, toTarget / length_, max(length_ - 0.01, 0.0));
    while (rayQueryProceedEXT(query)) {}
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) return -1.0;
    return rayQueryGetIntersectionTEXT(query, true);
}

// Whether the ray from origin to a light `size` across is blocked; narrows
// rtPenumbra to the penumbra this blocker casts.
bool lightRayBlocked(vec3 origin, vec3 target, float size) {
    float hit = segmentHit(origin, target);
    if (hit < 0.0) return false;
    float toLight = distance(origin, target);
    float penumbra = size * hit / max(toLight - hit, 1e-3);
    rtPenumbra = rtPenumbra > 0.0 ? min(rtPenumbra, penumbra) : penumbra;
    return true;
}

// Whether light i's sphere (centre lightPos, `radius`) is hidden from
// worldPos, as seen by one ray towards a random point of the disc it shows
// worldPos. Over frames TAA averages the rays into a penumbra as wide as the
// light is large. Emitter geometry should not cast shadows itself, or it
// would hide its own light.
bool localRayBlocked(uint i, vec3 worldPos, vec3 N, vec3 lightPos, float radius) {
    vec3 origin = worldPos + N * rtLightOffset;
    vec3 toLight = lightPos - origin;
    float dist = length(toLight);
    vec3 w = toLight / max(dist, 1e-4);

    vec3 target = lightPos;
    float spread = radius * showroom.lightSoftness;
#ifndef RT_HARD_LIGHT_SHADOWS
    if (spread > 0.0) {
        vec2 uv = lightRayNoise(i);
        vec3 t = normalize(cross(w, abs(w.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
        vec3 b = cross(w, t);
        float r = spread * sqrt(uv.x);
        float angle = uv.y * 6.28318531;
        target += (t * cos(angle) + b * sin(angle)) * r;
    }
#endif
    return lightRayBlocked(origin, target, 2.0 * spread);
}

// The same for a rectangular light (centre lightPos, half-axes halfX and
// halfY): a ray towards a random point of the rectangle, so the penumbra
// takes its shape, long beside a long softbox and short across it.
bool localRayBlockedRect(uint i, vec3 worldPos, vec3 N, vec3 lightPos, vec3 halfX, vec3 halfY) {
    vec3 origin = worldPos + N * rtLightOffset;
    vec3 target = lightPos;
#ifndef RT_HARD_LIGHT_SHADOWS
    vec2 uv = lightRayNoise(i) * 2.0 - 1.0;
    target += (halfX * uv.x + halfY * uv.y) * showroom.lightSoftness;
#endif
    return lightRayBlocked(origin, target, 2.0 * max(length(halfX), length(halfY)) * showroom.lightSoftness);
}

#endif
