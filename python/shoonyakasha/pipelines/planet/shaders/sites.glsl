// Flat sites on either body (world.Site): within a site's flat radius its
// body's ground lies at the site's height; out to its blend radius it eases
// into the body's own terrain, as the pad around the planet's site does
// (terrain.glsl). Distances are chords.
//
// Include after declaring the uniform block `terrain` with these fields of
// TerrainParams in pipeline.json after biomeView:
//     vec4 sites[TERRAIN_SITES];        // xyz: unit direction from the body's centre; w: height (m)
//     vec4 siteShapes[TERRAIN_SITES];   // flat radius (m), blend radius (m), body index, the body's
//                                       // radius (m); a blend radius of 0 leaves the entry unused

const int TERRAIN_SITES = 8;

// `height`, the terrain of body `body` at unit direction `dir`, with the sites on that body.
float terrainWithSites(float height, vec3 dir, int body) {
    for (int i = 0; i < TERRAIN_SITES; ++i) {
        vec4 shape = terrain.siteShapes[i];
        if (shape.y <= 0.0 || int(shape.z) != body) continue;
        float distance = length(dir - terrain.sites[i].xyz) * shape.w;
        if (distance < shape.y) {
            height = mix(terrain.sites[i].w, height, smoothstep(shape.x, shape.y, distance));
        }
    }
    return height;
}

// Whether unit direction `dir` on body `body` lies within a site's flat radius.
bool terrainOnSite(vec3 dir, int body) {
    for (int i = 0; i < TERRAIN_SITES; ++i) {
        vec4 shape = terrain.siteShapes[i];
        if (shape.y <= 0.0 || int(shape.z) != body) continue;
        if (length(dir - terrain.sites[i].xyz) * shape.w < shape.x) return true;
    }
    return false;
}
