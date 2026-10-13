// rtdgi_trace.glsl - one diffuse ray from a surface on screen. At the
// surface it hits it gathers the scene lights through shadow rays (with
// specular evaluated half way to rough), the surface's emission and, for
// light that bounced further, its albedo times either last frame's
// indirect light there (when the hit is on screen and was seen last frame)
// or the irradiance cache (rank 1). Past the scene it sees the sky.
//
// Included after common.glsl, ircache.glsl and lighting.glsl; the
// including shader declares the full resolution images `gPosition` and
// `reprojectedGi`.

#ifndef RTDGI_TRACE_GLSL
#define RTDGI_TRACE_GLSL

struct DiffuseTrace {
    vec3 radiance;
    vec3 hitNormal;
    float hitT;
    bool isHit;
};

DiffuseTrace traceDiffuse(vec3 origin, vec3 direction, float tMax, float coneSpread, inout uint rng) {
    DiffuseTrace result;
    result.radiance = vec3(0.0);
    result.hitNormal = -direction;
    result.hitT = tMax;
    result.isHit = false;

    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.0, direction, tMax);
    while (rayQueryProceedEXT(query)) {}
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        result.radiance = environment(direction);
        return result;
    }

    result.isHit = true;
    result.hitT = rayQueryGetIntersectionTEXT(query, true);
    SkHit hit = skFetchHit(query, coneSpread * (result.hitT + length(origin - camera.position.xyz)));
    vec3 V = -direction;
    vec3 N = hit.normal, Ng = hit.geometricNormal;
    if (dot(N, V) <= 0.0) N = Ng;
    result.hitNormal = N;

    // On screen, and the same surface the camera sees there?
    vec3 uvDepth = worldToUvDepth(hit.position);
    bool onScreen = all(greaterThanEqual(uvDepth.xy, vec2(0.0))) && all(lessThan(uvDepth.xy, vec2(1.0))) &&
                    uvDepth.z > 0.0;
    vec4 reprojected = vec4(0.0);
    if (onScreen) {
        ivec2 screenPx = ivec2(uvDepth.xy * camera.resolution);
        float screenDepth = imageLoad(gPosition, screenPx).w;
        onScreen = screenDepth > 0.0 && depthRelativeDiff(uvDepth.z, screenDepth) < 5e-3;
        if (onScreen) {
            reprojected = imageLoad(reprojectedGi, screenPx);
            onScreen = reprojected.w > 0.0;
        }
    }

    float roughness = mix(hit.roughness, 1.0, 0.5);
    result.radiance = directLight(hit.position, N, Ng, V, hit.baseColor, hit.metallic, roughness, rng);
    result.radiance += hit.emissive;
    if (onScreen) {
        result.radiance += reprojected.rgb * hit.baseColor;
    } else {
        IrcacheQuery q;
        q.from = origin;
        q.position = hit.position;
        q.normal = N;
        q.rank = 1u;
        q.jitter = false;
        result.radiance += ircacheLookup(q, rng) * hit.baseColor;
    }
    return result;
}

#endif
