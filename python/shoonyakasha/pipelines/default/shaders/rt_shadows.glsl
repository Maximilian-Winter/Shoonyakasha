// rt_shadows.glsl - sun shadow rays against the scene's acceleration
// structure. The including shader enables GL_EXT_ray_query, declares
// `accelerationStructureEXT scene` and the Settings block as `settings`, and
// includes sk/scene_hits.glsl for the alpha-tested casters.

#ifndef DEFAULT_RT_SHADOWS_GLSL
#define DEFAULT_RT_SHADOWS_GLSL

// Whether a ray from `origin` towards a point on the sun's disc hits
// anything. L points at the sun; u and v in [0, 1) pick the point, spread
// over a disc of angular radius settings.sunAngle.
bool sunRayBlocked(vec3 origin, vec3 L, float u, float v) {
    float radius = tan(radians(settings.sunAngle)) * sqrt(u);
    float angle = v * 6.28318531;
    vec3 t = normalize(cross(L, abs(L.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    vec3 b = cross(L, t);
    vec3 direction = normalize(L + (t * cos(angle) + b * sin(angle)) * radius);

    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.0, direction, 10000.0);
    // Opaque casters commit by themselves; the alpha-tested ones' triangles
    // come back as candidates.
    while (rayQueryProceedEXT(query)) {
        if (skAlphaTestCandidate(query)) rayQueryConfirmIntersectionEXT(query);
    }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

// How far off the surface a ray starts: a little more farther away, where
// depth is coarser.
float sunRayOffset(float viewDepth) {
    return 0.01 + 0.002 * viewDepth;
}

#endif
