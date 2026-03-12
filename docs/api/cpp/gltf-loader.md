# glTF Scene Loader

`Shoonyakasha::GltfSceneLoader` -- loads glTF 2.0 files into GPU resources and ECS entities with full PBR material support, skeletal animation, and texture deduplication.

> **One call, full scene.** Configure loading options, call `load()`, and get back GPU buffers, textures, ECS entities with components, skeletons, and animation clips -- ready to render.

**Header:** `#include "Resources/GltfSceneLoader.h"`
**Namespace:** `Shoonyakasha`

**Dependencies:** VulkanDevice (for GPU resource creation), ECS::Scene (optional, for entity creation), cgltf (internal glTF parsing).

---

## GltfLoadOptions

Configuration for the loading process. All fields have sensible defaults for typical PBR scenes.

```cpp
struct GltfLoadOptions {
    // What to load
    bool loadTextures       = true;
    bool loadMaterials      = true;
    bool createEntities     = true;
    bool loadSkins          = true;
    bool loadAnimations     = true;

    // Transform handling
    bool flattenHierarchy   = true;

    // Texture settings
    int  maxTextureSize     = 0;
    bool generateMipmaps    = true;
    bool srgbAlbedo         = true;

    // Naming
    std::string namePrefix  = "";
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `loadTextures` | `true` | Load and upload texture images to the GPU. When `false`, primitives will have invalid texture handles. |
| `loadMaterials` | `true` | Extract material parameters (base color factor, metallic/roughness, emissive). |
| `createEntities` | `true` | Create ECS entities with `MeshComponent`, `MaterialComponentV5`, and `RenderableTagComponent`. Requires a valid `Scene` pointer. |
| `loadSkins` | `true` | Load skeletal skin data (`Skeleton` objects with joint hierarchies and inverse bind matrices). |
| `loadAnimations` | `true` | Load animation clips (keyframe data for joints). Requires `loadSkins` to also be `true`. |
| `flattenHierarchy` | `true` | Bake node transforms into world space. For static meshes, this avoids needing a scene graph at runtime. Skinned meshes are not affected. |
| `maxTextureSize` | `0` | Maximum texture dimension in pixels. `0` means no limit. Textures exceeding this are downscaled during loading. |
| `generateMipmaps` | `true` | Generate mipmaps for loaded textures. |
| `srgbAlbedo` | `true` | Load albedo/base color textures in sRGB format. Normal maps and metallic-roughness maps are always loaded as linear. |
| `namePrefix` | `""` | Prefix prepended to entity and resource names. Useful when loading multiple models (e.g., `"sponza"`, `"character"`). |

---

## GltfPrimitive

A loaded primitive containing GPU-ready mesh data, textures, and material parameters. Each glTF mesh primitive produces one `GltfPrimitive`.

```cpp
struct GltfPrimitive {
    std::string name;

    // Geometry
    GPUBuffer vertexBuffer;
    GPUBuffer indexBuffer;
    uint32_t  vertexCount    = 0;
    uint32_t  indexCount     = 0;
    uint32_t  vertexStride   = 0;
    IndexType indexType      = IndexType::UInt32;

    // Textures
    GPUTexture albedoMap;
    GPUTexture normalMap;
    GPUTexture metallicRoughnessMap;
    GPUTexture aoMap;
    GPUTexture emissiveMap;

    // Material parameters
    glm::vec4 baseColorFactor   = glm::vec4(1.0f);
    float     metallicFactor    = 0.0f;
    float     roughnessFactor   = 0.5f;
    glm::vec3 emissiveFactor    = glm::vec3(0.0f);

    // Rendering properties
    AlphaMode alphaMode         = AlphaMode::Opaque;
    float     alphaCutoff       = 0.5f;
    bool      doubleSided       = false;

    // Transform
    glm::mat4 worldTransform    = glm::mat4(1.0f);

    // Convenience
    bool isTransparent() const;
    bool isMasked() const;
    bool isOpaque() const;
    bool hasIndices() const;
};
```

### Geometry Fields

| Field | Description |
|-------|-------------|
| `vertexBuffer` | GPU buffer containing interleaved vertex data (position, normal, UV, tangent). |
| `indexBuffer` | GPU index buffer. May be invalid if the primitive has no indices. |
| `vertexCount` | Number of vertices. |
| `indexCount` | Number of indices. `0` if unindexed. |
| `vertexStride` | Byte stride per vertex in the vertex buffer. |
| `indexType` | `IndexType::UInt16` or `IndexType::UInt32`. |

### Texture Fields

| Field | sRGB | Description |
|-------|------|-------------|
| `albedoMap` | Yes (if `srgbAlbedo`) | Base color / diffuse texture. |
| `normalMap` | No | Tangent-space normal map. |
| `metallicRoughnessMap` | No | Combined metallic (B) + roughness (G) texture. |
| `aoMap` | No | Ambient occlusion texture. |
| `emissiveMap` | Yes | Emissive glow texture. |

Any texture may be invalid (`.isValid() == false`) if the glTF material does not define it or `loadTextures` is `false`.

### Material Parameters

| Field | Default | Description |
|-------|---------|-------------|
| `baseColorFactor` | `(1, 1, 1, 1)` | Multiplied with the albedo texture. RGBA. |
| `metallicFactor` | `0.0` | Metallic value (0 = dielectric, 1 = metal). |
| `roughnessFactor` | `0.5` | Roughness value (0 = smooth/mirror, 1 = rough/diffuse). |
| `emissiveFactor` | `(0, 0, 0)` | Emissive color multiplier. |

### Rendering Properties

| Field | Default | Description |
|-------|---------|-------------|
| `alphaMode` | `Opaque` | `Opaque`, `Mask`, or `Blend`. Determines pipeline and sort order. |
| `alphaCutoff` | `0.5` | For `Mask` mode: fragments below this alpha are discarded. |
| `doubleSided` | `false` | If `true`, back-face culling should be disabled for this primitive. |

### Convenience Methods

| Method | Description |
|--------|-------------|
| `isTransparent()` | Returns `true` if `alphaMode == Blend`. |
| `isMasked()` | Returns `true` if `alphaMode == Mask`. |
| `isOpaque()` | Returns `true` if `alphaMode == Opaque`. |
| `hasIndices()` | Returns `true` if `indexBuffer` is valid and `indexCount > 0`. |

---

## GltfLoadResult

Everything produced by a `load()` call. Check `success` before using any other fields.

```cpp
struct GltfLoadResult {
    bool        success = false;
    std::string error;

    std::vector<GltfPrimitive>                          primitives;
    std::vector<entt::entity>                           entities;
    std::vector<std::shared_ptr<Skeleton>>              skeletons;
    std::vector<std::shared_ptr<AnimationClip>>         animationClips;

    size_t totalVertices   = 0;
    size_t totalIndices    = 0;
    size_t totalTextures   = 0;
    size_t totalMaterials  = 0;
};
```

| Field | Description |
|-------|-------------|
| `success` | `true` if loading completed without errors. |
| `error` | Error message if `success` is `false`. |
| `primitives` | All loaded primitives with GPU buffers, textures, and material data. |
| `entities` | ECS entity handles (only populated if `createEntities` is `true` and a `Scene` was provided). Each entity has `MeshComponent`, `MaterialComponentV5`, and `RenderableTagComponent`. Skinned entities also have `SkeletonComponent` and `AnimationPlaybackComponent`. |
| `skeletons` | Loaded skeleton data (joint hierarchies, inverse bind matrices). One per glTF skin. |
| `animationClips` | Animation clips (keyframes for joint transforms). |
| `totalVertices` | Total vertex count across all primitives. |
| `totalIndices` | Total index count across all primitives. |
| `totalTextures` | Number of unique textures loaded (after deduplication). |
| `totalMaterials` | Number of unique materials processed. |

---

## GltfSceneLoader

The loader class. Requires a `VulkanDevice` for GPU resource creation.

```cpp
class GltfSceneLoader {
public:
    explicit GltfSceneLoader(VulkanDevice& device);
    ~GltfSceneLoader();

    GltfLoadResult load(
        const std::filesystem::path& path,
        std::shared_ptr<ECS::Scene> scene = nullptr,
        const GltfLoadOptions& options = GltfLoadOptions{}
    );
};
```

### Constructor

```cpp
explicit GltfSceneLoader(VulkanDevice& device);
```

Creates a loader bound to a Vulkan device. The device is used for all GPU resource allocation (vertex buffers, index buffers, textures).

### load

```cpp
GltfLoadResult load(
    const std::filesystem::path& path,
    std::shared_ptr<ECS::Scene> scene = nullptr,
    const GltfLoadOptions& options = GltfLoadOptions{}
);
```

Loads a glTF 2.0 file (`.gltf` or `.glb`) and returns all produced resources.

**Parameters:**

| Parameter | Description |
|-----------|-------------|
| `path` | File path to the `.gltf` or `.glb` file. Relative texture paths are resolved from the file's directory. |
| `scene` | Optional ECS scene. If provided and `options.createEntities` is `true`, entities are created with mesh/material/renderable components. Pass `nullptr` to load primitives without creating entities. |
| `options` | Loading configuration. See [GltfLoadOptions](#gltfloadoptions). |

**Returns:** A `GltfLoadResult` with all loaded data. Always check `result.success` before using the results.

### Texture Deduplication

The loader maintains an internal texture cache keyed by file path. If multiple primitives reference the same texture file, it is uploaded to the GPU only once. The cache is reset on each `load()` call.

### Skinned Mesh Handling

When a glTF node has an associated skin:

1. A skinned vertex buffer is created (includes joint indices and weights per vertex).
2. A `Skeleton` is built from the skin's joint hierarchy and inverse bind matrices.
3. Animation clips are loaded if `loadAnimations` is `true`.
4. The entity receives `SkeletonComponent` and `AnimationPlaybackComponent` in addition to standard components.

Skins are cached per-load -- if multiple meshes share the same skin, the skeleton is loaded once.

---

## Usage Pattern

```cpp
GltfSceneLoader loader(device);

GltfLoadOptions options;
options.maxTextureSize   = 2048;
options.namePrefix       = "sponza";

auto result = loader.load("models/Sponza.gltf", scene, options);

if (result.success) {
    // Entities are ready with MeshComponent + MaterialComponentV5 + RenderableTagComponent
    auto& registry = scene->getRegistry();
    auto view = registry.view<MeshComponent, MaterialComponentV5>();
    for (auto entity : view) {
        // Draw with FrameGraphRenderer or manually bind and draw
    }

    // Statistics
    // result.totalVertices, result.totalIndices, result.totalTextures
}
```

### Loading Animated Characters

```cpp
GltfLoadOptions charOptions;
charOptions.loadSkins      = true;
charOptions.loadAnimations = true;
charOptions.namePrefix     = "character";

auto result = loader.load("models/character.glb", scene, charOptions);

if (result.success) {
    // Skeletons and animation clips are available
    for (auto& skeleton : result.skeletons) {
        // skeleton->jointCount(), skeleton->jointName(i), ...
    }
    for (auto& clip : result.animationClips) {
        // clip->name(), clip->duration(), ...
    }
    // Skinned entities already have SkeletonComponent + AnimationPlaybackComponent
}
```

### Loading Geometry Only (No Entities)

```cpp
auto result = loader.load("models/props.gltf", nullptr, {
    .createEntities = false,
    .loadSkins      = false,
    .loadAnimations = false
});

if (result.success) {
    for (auto& prim : result.primitives) {
        // Use prim.vertexBuffer, prim.indexBuffer, prim.albedoMap directly
    }
}
```
