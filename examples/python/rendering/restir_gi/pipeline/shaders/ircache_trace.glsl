// ircache_trace.glsl - one ray of a cache entry: from its trace origin
// towards a sample's direction. At the surface it hits, the radiance back
// along the ray is the scene lights through shadow rays, the surface's
// emission, and its albedo times the cache's irradiance there (looked up
// one rank further out); past the scene it is the sky. Included after
// common.glsl, ircache.glsl (with IRCACHE_LOOKUP_PRECISE) and lighting.glsl.

#ifndef IRCACHE_TRACE_GLSL
#define IRCACHE_TRACE_GLSL

struct IrcacheTraced {
    vec3 radiance;
    vec3 direction;
};

IrcacheTraced ircacheTrace(IrcacheVertex entry, uint params, uint life) {
    uint rng = sampleRng(params);
    IrcacheTraced result;
    result.direction = sampleDirection(params);

    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsOpaqueEXT, 0xFF, entry.position, 0.0, result.direction, RAY_FAR);
    while (rayQueryProceedEXT(query)) {}
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        result.radiance = environment(result.direction);
        return result;
    }

    // A ray cone spreading 0.1 radians picks the texture level.
    SkHit hit = skFetchHit(query, 0.1 * rayQueryGetIntersectionTEXT(query, true));
    vec3 V = -result.direction;
    vec3 N = hit.normal, Ng = hit.geometricNormal;
    if (dot(N, V) <= 0.0) N = Ng;
    // Rough-smooth paths make fireflies: specular is evaluated half way to rough.
    float roughness = mix(hit.roughness, 1.0, 0.5);

    vec3 radiance = directLight(hit.position, N, Ng, V, hit.baseColor, hit.metallic, roughness, rng);
    radiance += hit.emissive;

    IrcacheQuery q;
    q.from = entry.position;
    q.position = hit.position;
    q.normal = N;
    q.rank = 1u + ircacheLifeToRank(life);
    q.jitter = false;
    vec3 diffuse = hit.baseColor * (1.0 - hit.metallic);
    radiance += ircacheLookup(q, rng) * diffuse;

    result.radiance = radiance;
    return result;
}

// Rays towards the side the entry faces count fully; those behind it half,
// which damps light the entry would otherwise feed back to itself.
float selfLightingLimiter(vec3 direction, vec3 normal) {
    return mix(0.5, 1.0, smoothstep(-0.1, 0.0, dot(direction, normal)));
}

#endif
