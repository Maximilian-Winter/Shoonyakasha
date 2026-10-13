// rtr_trace.glsl - one reflection ray from a surface on screen. At the
// surface it hits, the light sent back along the ray is the scene lights
// through shadow rays (the hit's roughness biased towards 1 by the
// reflecting surface's alpha, which keeps rough-smooth paths from making
// fireflies), its emission and its albedo times the indirect diffuse light
// there: this frame's ReSTIR GI result when the hit is on screen and seen
// as the same surface, the irradiance cache otherwise. Past the scene it
// sees the sky.
//
// Included after common.glsl, ircache.glsl and lighting.glsl; the including
// shader declares the full resolution images `gPosition`, `gNormal` and
// `indirect` (this frame's indirect diffuse light).

#ifndef RTR_TRACE_GLSL
#define RTR_TRACE_GLSL

struct ReflectionTrace {
    vec3 radiance;
    vec3 hitNormal;
    float hitT;
};

ReflectionTrace traceReflection(vec3 origin, vec3 direction, float alpha, inout uint rng) {
    ReflectionTrace result;
    result.radiance = vec3(0.0);
    result.hitNormal = -direction;
    result.hitT = SKY_DIST;

    float coneSpread = sqrt(alpha) * 0.05;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.0, direction, SKY_DIST);
    while (rayQueryProceedEXT(query)) {}
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        result.radiance = environment(direction);
        return result;
    }

    result.hitT = rayQueryGetIntersectionTEXT(query, true);
    float eyeDistance = length(origin - camera.position.xyz);
    float pixelSpread = 2.0 * abs(camera.invProj[1][1]) / camera.resolution.y;
    float coneWidth = pixelSpread * eyeDistance + (pixelSpread + coneSpread) * result.hitT;
    SkHit hit = skFetchHit(query, coneWidth);
    vec3 V = -direction;
    vec3 N = hit.normal, Ng = hit.geometricNormal;
    if (dot(N, V) <= 0.0) N = Ng;
    result.hitNormal = N;

    // On screen, the same surface, facing the ray?
    vec3 uvDepth = worldToUvDepth(hit.position);
    bool onScreen = all(greaterThanEqual(uvDepth.xy, vec2(0.0))) && all(lessThan(uvDepth.xy, vec2(1.0))) &&
                    uvDepth.z > 0.0;
    ivec2 screenPx = ivec2(uvDepth.xy * camera.resolution);
    if (onScreen) {
        float screenDepth = imageLoad(gPosition, screenPx).w;
        vec3 screenNormal = imageLoad(gNormal, screenPx).xyz;
        onScreen = screenDepth > 0.0 && depthRelativeDiff(uvDepth.z, screenDepth) < 5e-3 &&
                   dot(screenNormal, -direction) > 0.0 && dot(screenNormal, N) > 0.7;
    }

    float hitAlpha = mix(hit.roughness * hit.roughness, 1.0, alpha);
    result.radiance = directLight(hit.position, N, Ng, V, hit.baseColor, hit.metallic, sqrt(hitAlpha), rng);
    result.radiance += hit.emissive;
    if (onScreen) {
        result.radiance += imageLoad(indirect, screenPx).rgb * hit.baseColor;
    } else {
        IrcacheQuery q;
        q.from = origin;
        q.position = hit.position;
        q.normal = N;
        q.rank = 1u;
        q.jitter = coneWidth < 0.1;
        result.radiance += ircacheLookup(q, rng) * hit.baseColor;
    }
    return result;
}

#endif
