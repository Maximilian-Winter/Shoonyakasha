//
// GltfSceneLoader.h - Load glTF files as ECS components
//
// 黃帝司中  調和而統御
// The Yellow Emperor governs the center — unifying meshes, materials, and entities
//

#pragma once

#include "ECS/Scene.h"
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"
#include "Resources/AnimationData.h"
#include "GPU/GPUTypes.h"
#include "GPU/GPUResourceFactory.h"

#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <optional>

// Forward declarations (cgltf internals)
struct cgltf_data;
struct cgltf_node;
struct cgltf_primitive;
struct cgltf_material;
struct cgltf_texture_view;
struct cgltf_skin;
struct cgltf_animation;

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// GltfPrimitive — Loaded primitive with thin GPU types
// 虚空之形 — Form arising from emptiness
// ═══════════════════════════════════════════════════════════════

struct GltfPrimitive {
    std::string name;

    // ─── Geometry (thin GPU wrappers) ───────────────────────
    Shoonyakasha::GPUBuffer vertexBuffer;
    Shoonyakasha::GPUBuffer indexBuffer;
    uint32_t vertexCount    = 0;
    uint32_t indexCount     = 0;
    uint32_t vertexStride   = 0;
    Shoonyakasha::IndexType indexType = Shoonyakasha::IndexType::UInt32;

    // ─── Textures (thin GPU wrappers) ───────────────────────
    Shoonyakasha::GPUTexture albedoMap;
    Shoonyakasha::GPUTexture normalMap;
    Shoonyakasha::GPUTexture metallicRoughnessMap;
    Shoonyakasha::GPUTexture aoMap;
    Shoonyakasha::GPUTexture emissiveMap;

    // ─── Material Parameters ────────────────────────────────
    glm::vec4 baseColorFactor   = glm::vec4(1.0f);
    float metallicFactor        = 0.0f;
    float roughnessFactor       = 0.5f;
    glm::vec3 emissiveFactor    = glm::vec3(0.0f);

    // ─── Rendering Properties ───────────────────────────────
    Shoonyakasha::AlphaMode alphaMode = Shoonyakasha::AlphaMode::Opaque;
    float alphaCutoff           = 0.5f;
    bool doubleSided            = false;

    // ─── Transforms ─────────────────────────────────────────
    glm::mat4 worldTransform    = glm::mat4(1.0f);

    // ─── Convenience ────────────────────────────────────────
    bool isTransparent() const { return alphaMode == Shoonyakasha::AlphaMode::Blend; }
    bool isMasked() const { return alphaMode == Shoonyakasha::AlphaMode::Mask; }
    bool isOpaque() const { return alphaMode == Shoonyakasha::AlphaMode::Opaque; }
    bool hasIndices() const { return indexBuffer.isValid() && indexCount > 0; }
};

// ═══════════════════════════════════════════════════════════════
// GltfLoadResult — Everything produced by loading a glTF file
// 載之果實 — The fruit of loading
// ═══════════════════════════════════════════════════════════════

struct GltfLoadResult {
    bool success = false;
    std::string error;

    // ─── Loaded Primitives (thin GPU types) ─────────────────
    std::vector<GltfPrimitive> primitives;

    // ─── ECS Entities ───────────────────────────────────────
    std::vector<entt::entity> entities;

    // ─── Skeleton & Animation Data ─────────────────────────
    std::vector<std::shared_ptr<Shoonyakasha::Skeleton>> skeletons;
    std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>> animationClips;

    // ─── Statistics ─────────────────────────────────────────
    size_t totalVertices      = 0;
    size_t totalIndices       = 0;
    size_t totalTextures      = 0;
    size_t totalMaterials     = 0;
};

// ═══════════════════════════════════════════════════════════════
// GltfLoadOptions — Configuration for the loading process
// 載之法門 — The method of loading
// ═══════════════════════════════════════════════════════════════

struct GltfLoadOptions {
    // What to load
    bool loadTextures           = true;
    bool loadMaterials          = true;
    bool createEntities         = true;
    bool loadSkins              = true;   // Load skeletal skin data
    bool loadAnimations         = true;   // Load animation clips

    // Transform handling
    bool flattenHierarchy       = true;   // Bake node transforms into vertices (static meshes only)

    // Texture settings
    int maxTextureSize          = 0;      // 0 = no limit
    bool generateMipmaps        = true;
    bool srgbAlbedo             = true;   // Load albedo as sRGB

    // Naming
    std::string namePrefix      = "";
};

// ═══════════════════════════════════════════════════════════════
// GltfSceneLoader — The loader itself
// 載之器 — The vessel of loading
// ═══════════════════════════════════════════════════════════════

class GltfSceneLoader {
public:
    explicit GltfSceneLoader(VulkanDevice& device);
    ~GltfSceneLoader();

    // ─── Main Loading Entry Point ───────────────────────────
    GltfLoadResult load(
        const std::filesystem::path& path,
        std::shared_ptr<ECS::Scene> scene = nullptr,
        const GltfLoadOptions& options = GltfLoadOptions{}
    );

private:
    VulkanDevice& m_device;

    // Per-load state (reset on each load call)
    std::filesystem::path m_basePath;
    GltfLoadOptions m_options;

    // Texture deduplication cache
    std::unordered_map<std::string, Shoonyakasha::GPUTexture> m_textureCache;

    // ─── Internal Processing ────────────────────────────────

    void processNode(
        cgltf_data* data,
        const cgltf_node* node,
        const glm::mat4& parentTransform,
        GltfLoadResult& result
    );

    /// Extract alpha mode from glTF material
    Shoonyakasha::AlphaMode extractAlphaMode(const cgltf_material* material);

    /// Resolve texture file path from glTF URI
    std::string resolveTexturePath(const std::string& uri);

    // ─── Internal Processing Methods ─────────────────────────

    /// Process a primitive (GPUBuffer + GPUTexture)
    GltfPrimitive processPrimitive(
        cgltf_data* data,
        const cgltf_primitive& primitive,
        const glm::mat4& worldTransform,
        const std::string& primitiveName
    );

    /// Build vertex GPUBuffer
    Shoonyakasha::GPUBuffer buildVertexBuffer(
        cgltf_data* data,
        const cgltf_primitive& primitive,
        const glm::mat4& worldTransform,
        uint32_t& outVertexCount,
        uint32_t& outVertexStride
    );

    /// Build index GPUBuffer
    Shoonyakasha::GPUBuffer buildIndexBuffer(
        cgltf_data* data,
        const cgltf_primitive& primitive,
        uint32_t& outIndexCount,
        Shoonyakasha::IndexType& outIndexType
    );

    /// Load a texture as GPUTexture
    Shoonyakasha::GPUTexture loadTexture(
        cgltf_data* data,
        const cgltf_texture_view& textureView,
        bool srgb
    );

    /// Create ECS entity with components
    entt::entity createEntity(
        std::shared_ptr<ECS::Scene> scene,
        const GltfPrimitive& primitive
    );

    // ─── Skin & Animation Loading ───────────────────────────

    /// Load a glTF skin as a Skeleton
    std::shared_ptr<Shoonyakasha::Skeleton> loadSkin(
        cgltf_data* data,
        const cgltf_skin* skin
    );

    /// Load all animations from glTF data
    std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>> loadAnimations(
        cgltf_data* data,
        const Shoonyakasha::Skeleton& skeleton
    );

    /// Build a skinned vertex buffer (with joints + weights)
    Shoonyakasha::GPUBuffer buildSkinnedVertexBuffer(
        cgltf_data* data,
        const cgltf_primitive& primitive,
        uint32_t& outVertexCount,
        uint32_t& outVertexStride
    );

    /// Create a skinned entity with SkeletonComponent + AnimationPlaybackComponent
    entt::entity createSkinnedEntity(
        std::shared_ptr<ECS::Scene> scene,
        const GltfPrimitive& primitive,
        std::shared_ptr<Shoonyakasha::Skeleton> skeleton,
        const std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>>& clips,
        const glm::mat4& worldTransform
    );

    /// Check if a node has an associated skin
    const cgltf_skin* getNodeSkin(const cgltf_node* node) const;

    /// Build a mapping from cgltf_node* to joint index for a skin
    std::unordered_map<const cgltf_node*, int> buildNodeToJointMap(
        const cgltf_skin* skin
    ) const;

    // Cached skeleton per skin (avoids reloading same skin for multiple meshes)
    std::unordered_map<const cgltf_skin*, std::shared_ptr<Shoonyakasha::Skeleton>> m_skinCache;

    // Default textures cache
    std::unique_ptr<Shoonyakasha::GPUResourceFactory::DefaultTextures> m_defaultTextures;
};

} // namespace Shoonyakasha
using Shoonyakasha::GltfPrimitive;
using Shoonyakasha::GltfLoadResult;
using Shoonyakasha::GltfLoadOptions;
using Shoonyakasha::GltfSceneLoader;

// ═══════════════════════════════════════════════════════════════
// Usage Example
// ═══════════════════════════════════════════════════════════════

/*

GltfSceneLoader loader(device);

auto result = loader.load("models/Sponza.gltf", scene, {
    .loadTextures = true,
    .maxTextureSize = 2048,
    .namePrefix = "sponza"
});

if (result.success) {
    // Entities have MeshComponent + MaterialComponentV5 + RenderableTagComponent
    auto& registry = scene->getRegistry();
    auto view = registry.view<MeshComponent, MaterialComponentV5>();
    for (auto entity : view) {
        // Bind and draw...
    }
}

*/
