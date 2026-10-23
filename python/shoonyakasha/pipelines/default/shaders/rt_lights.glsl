// rt_lights.glsl - shadow rays towards spot and point lights. Included by
// lights.glsl when RT_LOCAL_SHADOWS is defined. The including shader enables
// GL_EXT_ray_query, declares `accelerationStructureEXT scene` and the Camera
// block as `camera`, includes sk/scene_hits.glsl for the alpha-tested
// casters, and sets the two globals below before shading.

#ifndef DEFAULT_RT_LIGHTS_GLSL
#define DEFAULT_RT_LIGHTS_GLSL

// The pixel being shaded, for the noise that picks each ray's target, and
// how far off the surface rays start.
vec2 rtLightPixel = vec2(0.0);
float rtLightOffset = 0.01;

// Two numbers in 0..1 for light i's ray from this pixel, new each frame.
vec2 lightRayNoise(uint i) {
    vec2 pixel = rtLightPixel + vec2(37.0, 11.0) * float(i + 1u);
    return vec2(temporalNoise(pixel, camera.frame + i, camera.taa),
                temporalNoise(pixel.yx + 17.0, camera.frame + i + 3u, camera.taa));
}

// Whether anything lies between origin and target.
bool segmentBlocked(vec3 origin, vec3 target) {
    vec3 toTarget = target - origin;
    float length_ = length(toTarget);
    if (length_ <= 1e-3) return false;

    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.0,
                          toTarget / length_, max(length_ - 0.01, 0.0));
    // Opaque casters commit by themselves; the alpha-tested ones' triangles
    // come back as candidates.
    while (rayQueryProceedEXT(query)) {
        if (skAlphaTestCandidate(query)) rayQueryConfirmIntersectionEXT(query);
    }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

// 1 when one ray from worldPos reaches a random point of light i, 0 when
// something is in the way. The point lies on the light's rectangle
// (sceneLights[i].shape), or else on the disc of its source radius
// (sceneLights[i].source.x) facing worldPos, or is its centre for radius 0.
// Over frames TAA averages the rays into a penumbra as large as the light.
// Geometry at the light itself should not cast shadows, or it hides the
// light.
float localRayVisibility(uint i, vec3 worldPos, vec3 N) {
    vec3 origin = worldPos + N * rtLightOffset;
    vec3 target = sceneLights[i].positionType.xyz;
    vec4 shape = sceneLights[i].shape;
    vec2 uv = lightRayNoise(i);
    if (shape.w > 0.0) {
        vec3 forward = normalize(sceneLights[i].directionRange.xyz);
        vec3 halfY = normalize(cross(shape.xyz, forward)) * shape.w;
        target += shape.xyz * (uv.x * 2.0 - 1.0) + halfY * (uv.y * 2.0 - 1.0);
    } else {
        float radius = sceneLights[i].source.x;
        if (radius > 0.0) {
            vec3 w = normalize(target - origin);
            vec3 t = normalize(cross(w, abs(w.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
            vec3 b = cross(w, t);
            float r = radius * sqrt(uv.x);
            float angle = uv.y * 6.28318531;
            target += (t * cos(angle) + b * sin(angle)) * r;
        }
    }
    return segmentBlocked(origin, target) ? 0.0 : 1.0;
}

#endif
