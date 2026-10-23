// material_uv.glsl - which uv each material texture is sampled at
// (pipeline.json "MaterialUV", in the material set at binding 9).
//
// Per texture, two rows map (u, v, 1) to the uv sampled; row0.w picks the
// uv set: 0 TEXCOORD_0, 1 TEXCOORD_1. Without TEXCOORD_1 or
// KHR_texture_transform the rows are (1, 0, 0, 0) and (0, 1, 0, 0).

#ifndef DEFAULT_MATERIAL_UV_GLSL
#define DEFAULT_MATERIAL_UV_GLSL

layout(set = 1, binding = 9) uniform MaterialUV {
    vec4 albedoUv0, albedoUv1;
    vec4 metallicRoughnessUv0, metallicRoughnessUv1;
    vec4 normalUv0, normalUv1;
    vec4 occlusionUv0, occlusionUv1;
    vec4 emissiveUv0, emissiveUv1;
    vec4 clearcoatUv0, clearcoatUv1;
    vec4 clearcoatRoughnessUv0, clearcoatRoughnessUv1;
    vec4 transmissionUv0, transmissionUv1;
    vec4 thicknessUv0, thicknessUv1;
} materialUv;

// The uv a texture with these rows is sampled at, from the two uv sets.
vec2 textureUv(vec4 row0, vec4 row1, vec2 uv0, vec2 uv1) {
    vec3 uv = vec3(row0.w > 0.5 ? uv1 : uv0, 1.0);
    return vec2(dot(row0.xyz, uv), dot(row1.xyz, uv));
}

// Whether the rows sample TEXCOORD_0 as it is.
bool isPlainUv(vec4 row0, vec4 row1) {
    return row0 == vec4(1.0, 0.0, 0.0, 0.0) && row1 == vec4(0.0, 1.0, 0.0, 0.0);
}

#endif
