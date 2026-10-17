// glslc: --target-env=vulkan1.2
// sk/scene_hits.glsl - the surface a ray query hit: position, normals, uv,
// glTF metallic-roughness material, transmission, index of refraction and
// thin-film iridescence
//
//     #define SK_SCENE_HITS_SET 2              // set of the two bindings below (default 0)
//     #define SK_RAY_INSTANCES_BINDING 0       // "source": {"type": "scene", "data": "rayInstances"} (default 0)
//     #define SK_SCENE_TEXTURES_BINDING 1      // "source": "sceneTextures" (default 1)
//     #include "sk/scene_hits.glsl"
//
//     SkHit hit = skFetchHit(query, coneWidth);   // after a committed triangle hit
//
// The including shader compiles for Vulkan 1.2 or later: `// glslc:
// --target-env=vulkan1.2` on its second line. Row i of the instances buffer
// describes instance i of the acceleration structure (its instance id, not
// its custom index).
//

#ifndef SK_SCENE_HITS_GLSL
#define SK_SCENE_HITS_GLSL

#ifndef SK_SCENE_HITS_SET
#define SK_SCENE_HITS_SET 0
#endif
#ifndef SK_RAY_INSTANCES_BINDING
#define SK_RAY_INSTANCES_BINDING 0
#endif
#ifndef SK_SCENE_TEXTURES_BINDING
#define SK_SCENE_TEXTURES_BINDING 1
#endif

#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_nonuniform_qualifier : require

#define SK_NO_TEXTURE 0xFFFFFFFFu
#define SK_INDEX_NONE 2u
#define SK_DOUBLE_SIDED 1u

struct SkRayInstance {
    uvec4 geometry;    // vertex buffer address (lo, hi), index buffer address (lo, hi)
    uvec4 vertexLayout;      // vertex stride, index type (0 16-bit, 1 32-bit, 2 none), offsets, flags
    vec4 baseColor;
    vec4 emissive;
    vec4 surface;      // metallic, roughness, alpha cutoff, entity id (bits)
    uvec4 textures;    // albedo, normal, metallicRoughness, emissive slots
    vec4 optics;       // transmission, ior, iridescence factor, iridescence ior
    vec4 film;         // thickness minimum, maximum (nm), iridescence thickness and iridescence slots (bits)
};

layout(set = SK_SCENE_HITS_SET, binding = SK_RAY_INSTANCES_BINDING, std430) readonly buffer SkRayInstances {
    SkRayInstance skRayInstances[];
};
layout(set = SK_SCENE_HITS_SET, binding = SK_SCENE_TEXTURES_BINDING) uniform sampler2D skSceneTextures[];

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer SkWords { uint v[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer SkFloats { float v[]; };

struct SkHit {
    vec3 position;          // world space
    vec3 normal;            // shading normal (normal map applied), world space, facing the ray's origin
    vec3 geometricNormal;   // the triangle's, world space, facing the ray's origin
    vec2 uv;
    vec3 baseColor;         // linear
    float alpha;
    vec3 emissive;
    float metallic;
    float roughness;
    bool frontFace;
    uint instance;          // row of skRayInstances
    uint entity;
    float transmission;     // KHR_materials_transmission factor
    float ior;              // KHR_materials_ior
    float iridescence;      // KHR_materials_iridescence: amount of the thin film (0 none)
    float filmIor;          // the film's index of refraction
    float filmThickness;    // the film's thickness here, nanometres
};

uint skIndex(SkRayInstance row, uint i) {
    uint type = row.vertexLayout.y;
    if (type == SK_INDEX_NONE) return i;
    SkWords words = SkWords(row.geometry.zw);
    if (type == 1u) return words.v[i];
    uint word = words.v[i >> 1];
    return (i & 1u) == 0u ? word & 0xFFFFu : word >> 16;
}

vec3 skVertexVec3(SkFloats floats, uint vertex, uint strideWords, uint offsetBytes) {
    uint base = vertex * strideWords + offsetBytes / 4u;
    return vec3(floats.v[base], floats.v[base + 1u], floats.v[base + 2u]);
}

vec2 skVertexVec2(SkFloats floats, uint vertex, uint strideWords, uint offsetBytes) {
    uint base = vertex * strideWords + offsetBytes / 4u;
    return vec2(floats.v[base], floats.v[base + 1u]);
}

// A scene texture at uv, at the level of detail of a ray cone `coneWidth`
// wide at the hit, where `lodBase` is half the log2 of the triangle's uv area
// over its world area and `cosine` the cosine between the ray and the normal.
// coneWidth 0 reads the full-size level.
vec4 skSceneTexture(uint slot, vec2 uv, float lodBase, float coneWidth, float cosine) {
    ivec2 size = textureSize(skSceneTextures[nonuniformEXT(slot)], 0);
    float lod = coneWidth > 0.0
        ? lodBase + 0.5 * log2(float(size.x * size.y)) + log2(coneWidth / max(cosine, 1e-3))
        : 0.0;
    return textureLod(skSceneTextures[nonuniformEXT(slot)], uv, max(lod, 0.0));
}

// The committed hit of `query`, which must be a triangle.
SkHit skFetchHit(rayQueryEXT query, float coneWidth) {
    SkHit hit;
    hit.instance = uint(rayQueryGetIntersectionInstanceIdEXT(query, true));
    SkRayInstance row = skRayInstances[hit.instance];
    hit.entity = floatBitsToUint(row.surface.w);

    uint primitive = uint(rayQueryGetIntersectionPrimitiveIndexEXT(query, true));
    uint i0 = skIndex(row, primitive * 3u);
    uint i1 = skIndex(row, primitive * 3u + 1u);
    uint i2 = skIndex(row, primitive * 3u + 2u);
    vec2 bary2 = rayQueryGetIntersectionBarycentricsEXT(query, true);
    vec3 bary = vec3(1.0 - bary2.x - bary2.y, bary2.x, bary2.y);

    SkFloats floats = SkFloats(row.geometry.xy);
    uint strideWords = row.vertexLayout.x / 4u;
    uint normalOffset = row.vertexLayout.z & 0xFFu;
    uint uvOffset = (row.vertexLayout.z >> 8) & 0xFFu;
    uint colorOffset = (row.vertexLayout.z >> 16) & 0xFFu;

    vec3 p0 = skVertexVec3(floats, i0, strideWords, 0u);
    vec3 p1 = skVertexVec3(floats, i1, strideWords, 0u);
    vec3 p2 = skVertexVec3(floats, i2, strideWords, 0u);
    vec3 n = skVertexVec3(floats, i0, strideWords, normalOffset) * bary.x +
             skVertexVec3(floats, i1, strideWords, normalOffset) * bary.y +
             skVertexVec3(floats, i2, strideWords, normalOffset) * bary.z;
    vec2 t0 = skVertexVec2(floats, i0, strideWords, uvOffset);
    vec2 t1 = skVertexVec2(floats, i1, strideWords, uvOffset);
    vec2 t2 = skVertexVec2(floats, i2, strideWords, uvOffset);
    vec3 color = skVertexVec3(floats, i0, strideWords, colorOffset) * bary.x +
                 skVertexVec3(floats, i1, strideWords, colorOffset) * bary.y +
                 skVertexVec3(floats, i2, strideWords, colorOffset) * bary.z;
    hit.uv = t0 * bary.x + t1 * bary.y + t2 * bary.z;

    mat4x3 toWorld = rayQueryGetIntersectionObjectToWorldEXT(query, true);
    mat3 normalToWorld = transpose(mat3(rayQueryGetIntersectionWorldToObjectEXT(query, true)));
    hit.position = toWorld * vec4(p0 * bary.x + p1 * bary.y + p2 * bary.z, 1.0);
    vec3 e1 = mat3(toWorld) * (p1 - p0), e2 = mat3(toWorld) * (p2 - p0);
    vec3 faceCross = cross(e1, e2);
    vec3 Ng = normalize(faceCross);
    vec3 N = normalize(normalToWorld * n);
    if (any(isnan(N))) N = Ng;

    // Both normals face the side the ray came from.
    vec3 direction = rayQueryGetWorldRayDirectionEXT(query);
    hit.frontFace = rayQueryGetIntersectionFrontFaceEXT(query, true);
    if (dot(Ng, direction) > 0.0) Ng = -Ng;
    if (dot(N, Ng) < 0.0) N = -N;
    float cosine = abs(dot(Ng, normalize(direction)));

    // Texture level of detail from the triangle's uv density.
    vec2 d1 = t1 - t0, d2 = t2 - t0;
    float uvArea = abs(d1.x * d2.y - d1.y * d2.x);
    float worldArea = length(faceCross);
    float lodBase = 0.5 * log2(max(uvArea, 1e-20) / max(worldArea, 1e-20));

    vec4 base = row.baseColor * vec4(color, 1.0);
    if (row.textures.x != SK_NO_TEXTURE) base *= skSceneTexture(row.textures.x, hit.uv, lodBase, coneWidth, cosine);
    hit.baseColor = base.rgb;
    hit.alpha = base.a;

    hit.metallic = row.surface.x;
    hit.roughness = row.surface.y;
    if (row.textures.z != SK_NO_TEXTURE) {
        vec4 mr = skSceneTexture(row.textures.z, hit.uv, lodBase, coneWidth, cosine);   // g roughness, b metallic
        hit.metallic *= mr.b;
        hit.roughness *= mr.g;
    }
    hit.metallic = clamp(hit.metallic, 0.0, 1.0);
    hit.roughness = clamp(hit.roughness, 0.045, 1.0);

    hit.emissive = row.emissive.rgb;
    if (row.textures.w != SK_NO_TEXTURE) {
        hit.emissive *= skSceneTexture(row.textures.w, hit.uv, lodBase, coneWidth, cosine).rgb;
    }

    // The thin film: thickness from minimum to maximum by the thickness
    // texture's g (the maximum without one), amount by the factor times the
    // iridescence texture's r.
    hit.transmission = row.optics.x;
    hit.ior = row.optics.y;
    hit.iridescence = row.optics.z;
    hit.filmIor = row.optics.w;
    hit.filmThickness = row.film.y;
    uint thicknessSlot = floatBitsToUint(row.film.z), iridescenceSlot = floatBitsToUint(row.film.w);
    if (hit.iridescence > 0.0 && thicknessSlot != SK_NO_TEXTURE) {
        float g = skSceneTexture(thicknessSlot, hit.uv, lodBase, coneWidth, cosine).g;
        hit.filmThickness = mix(row.film.x, row.film.y, g);
    }
    if (hit.iridescence > 0.0 && iridescenceSlot != SK_NO_TEXTURE) {
        hit.iridescence *= skSceneTexture(iridescenceSlot, hit.uv, lodBase, coneWidth, cosine).r;
    }

    // Normal map, in the tangent frame of the triangle's uvs.
    if (row.textures.y != SK_NO_TEXTURE) {
        float det = d1.x * d2.y - d1.y * d2.x;
        if (abs(det) > 1e-20) {
            vec3 T = (e1 * d2.y - e2 * d1.y) / det;
            vec3 B = (e2 * d1.x - e1 * d2.x) / det;
            T = normalize(T - N * dot(N, T));
            B = normalize(B - N * dot(N, B) - T * dot(T, B));
            vec3 m = skSceneTexture(row.textures.y, hit.uv, lodBase, coneWidth, cosine).xyz * 2.0 - 1.0;
            vec3 mapped = mat3(T, B, N) * m;
            if (dot(mapped, mapped) > 1e-6 && !any(isnan(mapped))) N = normalize(mapped);
        }
    }
    hit.normal = N;
    hit.geometricNormal = Ng;
    return hit;
}

#endif
