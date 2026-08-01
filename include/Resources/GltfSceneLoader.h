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
#include <unordered_set>
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
    // Shared and reference-counted: copying a primitive shares its geometry, and
    // the allocation is freed when the last holder — usually a MeshComponent — is
    // gone.
    Shoonyakasha::GpuBufferRef vertexBuffer;
    Shoonyakasha::GpuBufferRef indexBuffer;
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
    bool hasIndices() const { return indexBuffer && indexBuffer->isValid() && indexCount > 0; }
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

    /// One entity per glTF node, in traversal order. Nodes carry the scene graph;
    /// the renderable entities in `entities` are their children.
    std::vector<entt::entity> nodeEntities;

    /// The top-level node entities. Parent something to one of these, or move it,
    /// and the whole loaded scene follows.
    std::vector<entt::entity> rootEntities;

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

    // Transform handling.
    //
    // false (default): keep the glTF node tree. One entity per node carrying its
    //   local transform, geometry entities parented beneath, vertex buffers in
    //   mesh space and therefore SHARED between every node referencing the same
    //   mesh. Moving an entity rotates about its own origin.
    //
    // true: the old behaviour — bake each node's world transform into its vertices
    //   and emit a flat entity list with identity transforms. No sharing is
    //   possible, since every instance needs differently-transformed vertices.
    //
    // This flag was declared, plumbed through the facade and Python, and
    // documented, but no code ever read it; baking was unconditional.
    bool flattenHierarchy       = false;

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

    /// Drop the shared-geometry cache.
    ///
    /// The cache holds a reference to every mesh primitive it has built, which is
    /// what lets a second load of the same file reuse the buffers — and also what
    /// keeps them alive after the last entity using them is destroyed. Without
    /// this, geometry accumulates for the lifetime of the loader, which matters
    /// for anything that loads more than one level.
    ///
    /// Safe at any time: primitives still referenced by a MeshComponent stay
    /// alive, and the rest retire through the delete queue in the usual way.
    /// Reloading the file afterwards simply rebuilds them.
    ///
    /// Textures are deliberately NOT dropped here — GPUTexture is not
    /// reference-counted, and materials hold its views and samplers directly.
    void releaseCachedGeometry();

private:
    VulkanDevice& m_device;

    // Per-load state (reset on each load call)
    std::filesystem::path m_basePath;
    std::filesystem::path m_currentFile;  // Scopes cache keys for embedded textures
    GltfLoadOptions m_options;

    /// Texture deduplication cache. Owns every GPUTexture it holds and lives for
    /// the loader's lifetime — it deliberately is NOT cleared per load, because
    /// entities from an earlier load still reference these views and samplers.
    /// Freed only in ~GltfSceneLoader.
    std::unordered_map<std::string, Shoonyakasha::GPUTexture> m_textureCache;

    /// Cache keys touched by the load in progress, so GltfLoadResult::totalTextures
    /// stays per-load now that the cache spans loads.
    std::unordered_set<std::string> m_loadTextureKeys;

    /// Mesh primitives in local space, keyed by file, mesh index, primitive index
    /// and skinned-ness. Two nodes referencing the same glTF mesh get the same
    /// GpuBufferRef and cost one allocation. Buffers are reference-counted, so this
    /// map holding a copy is exactly what keeps them alive between loads.
    std::unordered_map<std::string, GltfPrimitive> m_primitiveCache;

    /// Destroy every cached texture. Only safe once nothing references them —
    /// i.e. at destruction, after the scene holding the materials is gone.
    void destroyTextureCache();

    // ─── Internal Processing ────────────────────────────────

    /// Walk one node and its subtree, creating an entity per node that carries the
    /// node's LOCAL transform and a link to its parent. Geometry entities hang off
    /// those, with an identity transform.
    ///
    /// Local rather than world on purpose: glTF forbids shear in a node's own
    /// transform, so a local transform always survives the trip through
    /// TransformComponent's position/euler/scale exactly. Their *product* can
    /// shear, which is why the world matrix is composed by TransformSystem and
    /// never decomposed here.
    void processNode(
        cgltf_data* data,
        const cgltf_node* node,
        entt::entity parentEntity,
        std::shared_ptr<ECS::Scene> scene,
        GltfLoadResult& result
    );

    /// Set a TransformComponent from a glTF node, preferring the node's own TRS
    /// over decomposing its matrix.
    void applyNodeTransform(ECS::TransformComponent& transform, const cgltf_node* node);

    /// Geometry and material for one glTF mesh primitive, in mesh-local space and
    /// therefore independent of which node references it. Built once per
    /// (file, mesh, primitive) and shared by every instance.
    const GltfPrimitive* getOrBuildPrimitive(
        cgltf_data* data,
        const cgltf_node* node,
        size_t primitiveIndex,
        bool skinned,
        const std::string& name
    );

    /// Build one primitive's geometry and material, applying `transform` to the
    /// vertices. Pass identity for the shareable mesh-local form.
    GltfPrimitive buildPrimitive(
        cgltf_data* data,
        const cgltf_node* node,
        size_t primitiveIndex,
        bool skinned,
        const std::string& name,
        const glm::mat4& transform
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
        const GltfPrimitive& primitive,
        entt::entity parentEntity
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
        entt::entity parentEntity
    );

    /// Check if a node has an associated skin
    const cgltf_skin* getNodeSkin(const cgltf_node* node) const;

    /// Build a mapping from cgltf_node* to joint index for a skin
    std::unordered_map<const cgltf_node*, int> buildNodeToJointMap(
        const cgltf_skin* skin
    ) const;

    // Cached skeleton per skin (avoids reloading same skin for multiple meshes)
    std::unordered_map<const cgltf_skin*, std::shared_ptr<Shoonyakasha::Skeleton>> m_skinCache;

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
