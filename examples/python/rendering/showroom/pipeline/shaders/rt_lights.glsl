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
#ifndef RT_HARD_LIGHT_SHADOWS
    float spread = radius * showroom.lightSoftness;
    if (spread > 0.0) {
        float u = temporalNoise(rtLightPixel + vec2(37.0, 11.0) * float(i + 1u), camera.frame + i, camera.taa);
        float v = temporalNoise(rtLightPixel.yx + vec2(5.0, 71.0) * float(i + 1u), camera.frame + 7u * i + 3u, camera.taa);
        vec3 t = normalize(cross(w, abs(w.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
        vec3 b = cross(w, t);
        float r = spread * sqrt(u);
        float angle = v * 6.28318531;
        target += (t * cos(angle) + b * sin(angle)) * r;
    }
#endif
    vec3 toTarget = target - origin;
    float length_ = length(toTarget);
    if (length_ <= 1e-3) return false;

    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                          0xFF, origin, 0.0, toTarget / length_, max(length_ - 0.01, 0.0));
    while (rayQueryProceedEXT(query)) {}
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

#endif
