// glslc: --target-env=vulkan1.2
// sk/scene_textures.glsl - the scene's textures by slot: the array of a
// combined_image_sampler binding with "source": "sceneTextures", indexed by
// the slots of entity.material.textures.<name>.slot or of a rayInstances row
//
//     #define SK_SCENE_TEXTURES_SET 1         // set of the array (default 0)
//     #define SK_SCENE_TEXTURES_BINDING 0     // its binding (default 1)
//     #include "sk/scene_textures.glsl"
//
//     vec4 albedo = skSceneTextureOr(pc.albedoSlot, uv, vec4(1.0));
//
// The including shader compiles for Vulkan 1.2 or later: `// glslc:
// --target-env=vulkan1.2` on its second line.
//

#ifndef SK_SCENE_TEXTURES_GLSL
#define SK_SCENE_TEXTURES_GLSL

#ifndef SK_SCENE_TEXTURES_SET
#define SK_SCENE_TEXTURES_SET 0
#endif
#ifndef SK_SCENE_TEXTURES_BINDING
#define SK_SCENE_TEXTURES_BINDING 1
#endif

#extension GL_EXT_nonuniform_qualifier : require

#define SK_NO_TEXTURE 0xFFFFFFFFu

layout(set = SK_SCENE_TEXTURES_SET, binding = SK_SCENE_TEXTURES_BINDING) uniform sampler2D skSceneTextures[];

// The texture at `slot` sampled at uv, or `fallback` for SK_NO_TEXTURE.
vec4 skSceneTextureOr(uint slot, vec2 uv, vec4 fallback) {
    return slot == SK_NO_TEXTURE ? fallback : texture(skSceneTextures[nonuniformEXT(slot)], uv);
}

#endif
