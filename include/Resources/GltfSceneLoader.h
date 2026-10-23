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
#include "ECS/MorphComponents.h"
#include "Resources/AnimationData.h"
#include "GPU/GPUTypes.h"
#include "GPU/GPUResourceFactory.h"

#include <filesystem>
#include <array>
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
struct cgltf_mesh;
struct cgltf_material;
struct cgltf_texture_view;
struct cgltf_sampler;
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
    // Generated levels of detail (GltfLoadOptions::generateLods), after the
    // full mesh's indexCount indices in the index buffer.
    std::vector<Shoonyakasha::MeshLod> lods;
    // The primitive's morph targets and its vertices with none applied; null
    // when it has none.
    std::shared_ptr<const Shoonyakasha::MorphTargetSet> morphTargets;

    // ─── Textures (thin GPU wrappers) ───────────────────────
    Shoonyakasha::GPUTexture albedoMap;
    Shoonyakasha::GPUTexture normalMap;
    Shoonyakasha::GPUTexture metallicRoughnessMap;
    Shoonyakasha::GPUTexture aoMap;
    Shoonyakasha::GPUTexture emissiveMap;
    Shoonyakasha::GPUTexture clearcoatMap;            // KHR_materials_clearcoat: r amount
    Shoonyakasha::GPUTexture clearcoatRoughnessMap;   // g roughness
    Shoonyakasha::GPUTexture transmissionMap;         // KHR_materials_transmission: r amount
    Shoonyakasha::GPUTexture thicknessMap;            // KHR_materials_volume: g thickness
    Shoonyakasha::GPUTexture iridescenceMap;           // KHR_materials_iridescence: r amount
    Shoonyakasha::GPUTexture iridescenceThicknessMap;  // g thickness, from minimum to maximum
    Shoonyakasha::GPUTexture sheenColorMap;            // KHR_materials_sheen: rgb colour (sRGB)
    Shoonyakasha::GPUTexture sheenRoughnessMap;        // a roughness
    Shoonyakasha::GPUTexture specularMap;              // KHR_materials_specular: a strength
    Shoonyakasha::GPUTexture specularColorMap;         // rgb colour (sRGB)
    // Textures read from TEXCOORD_1 or through KHR_texture_transform: the
    // slot and two rows, (m00, m01, m02, uv set) and (m10, m11, m12, 0), that
    // map (u, v, 1) to the uv sampled. The material gets them as the params
    // <slot>UvRow0 and <slot>UvRow1.
    std::vector<std::pair<std::string, std::array<glm::vec4, 2>>> uvTransforms;

    // ─── Material Parameters ────────────────────────────────
    glm::vec4 baseColorFactor   = glm::vec4(1.0f);
    float metallicFactor        = 0.0f;
    float roughnessFactor       = 0.5f;
    glm::vec3 emissiveFactor    = glm::vec3(0.0f);   // times KHR_materials_emissive_strength
    float normalScale           = 1.0f;   // normalTexture.scale
    float occlusionStrength     = 1.0f;   // occlusionTexture.strength
    // KHR_materials_clearcoat factors, when the material has the extension.
    // Its coat normal map is not read: the coat takes the base normal.
    bool hasClearcoat           = false;
    float clearcoatFactor       = 0.0f;
    float clearcoatRoughnessFactor = 0.0f;
    // KHR_materials_transmission and KHR_materials_ior, with the extensions'
    // defaults when a material has neither.
    float transmissionFactor    = 0.0f;
    float ior                   = 1.5f;
    // KHR_materials_volume: thickness in the mesh's units (0 thin-walled),
    // and the distance over which light keeps attenuationColor of itself
    // (0 for none).
    float thicknessFactor       = 0.0f;
    float attenuationDistance   = 0.0f;
    glm::vec3 attenuationColor  = glm::vec3(1.0f);
    // KHR_materials_sheen, when the material has the extension.
    bool hasSheen               = false;
    glm::vec3 sheenColorFactor  = glm::vec3(0.0f);
    float sheenRoughnessFactor  = 0.0f;
    // KHR_materials_specular, when the material has the extension.
    bool hasSpecular            = false;
    float specularFactor        = 1.0f;
    glm::vec3 specularColorFactor = glm::vec3(1.0f);
    // KHR_materials_iridescence, when the material has the extension: a thin
    // film of index iridescenceIor, thicknesses in nanometres.
    // KHR_materials_unlit: the material gets the param unlit = 1.
    bool unlit                  = false;
    bool hasIridescence         = false;
    float iridescenceFactor     = 0.0f;
    float iridescenceIor        = 1.3f;
    float iridescenceThicknessMinimum = 100.0f;
    float iridescenceThicknessMaximum = 400.0f;

    // ─── Rendering Properties ───────────────────────────────
    Shoonyakasha::AlphaMode alphaMode = Shoonyakasha::AlphaMode::Opaque;
    float alphaCutoff           = 0.5f;
    bool doubleSided            = false;

    // ─── Transforms ─────────────────────────────────────────
    glm::mat4 worldTransform    = glm::mat4(1.0f);

    // ─── Bounds ─────────────────────────────────────────────
    // Of the vertices as uploaded (transform baked in when flattened; bind
    // pose for skinned meshes).
    glm::vec3 boundsMin         = glm::vec3(0.0f);
    glm::vec3 boundsMax         = glm::vec3(0.0f);
    bool      hasBounds         = false;

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

    /// With GltfLoadOptions::loadLights, one entity per KHR_lights_punctual
    /// light a node holds, a child of the node's entity (unparented, at the
    /// node's world position and direction, with flattenHierarchy).
    std::vector<entt::entity> lights;

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
    // KHR_lights_punctual: an entity with a LightComponent for each node's
    // light (GltfLoadResult::lights).
    bool loadLights             = false;

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
    int maxTextureSize          = 0;      // larger images are halved until they fit; 0 = no limit
    bool generateMipmaps        = true;
    // Block-compress textures where the device samples BC formats: colour
    // BC1 (BC3 with transparency), normal maps BC5 (x and y), other data BC1.
    bool compressTextures       = false;
    bool srgbAlbedo             = true;   // Load albedo as sRGB

    // Generate up to four coarser levels of detail for each static triangle
    // mesh (MeshComponent::lods; appendMeshLods), in its index buffer.
    // Skinned and morphed meshes get none.
    bool generateLods           = false;

    // Naming
    std::string namePrefix      = "";
};

// ═══════════════════════════════════════════════════════════════
// Meshes from arrays
// ═══════════════════════════════════════════════════════════════

/// Triangles for GltfSceneLoader::createMeshEntity and setMeshGeometry:
/// every three indices a triangle, counter-clockwise from the front, or
/// without indices every three vertices. Normals, uvs, colours and tangents
/// are empty or one per position. Without tangents, meshes with uvs get
/// MikkTSpace tangents (MeshTangents.h).
struct MeshArrays {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> colors;
    std::vector<glm::vec4> tangents;   // xyz along +u, w = ±1 (bitangent = w * cross(normal, xyz))
    std::vector<uint32_t>  indices;
};

// ═══════════════════════════════════════════════════════════════
// GltfSceneLoader — The loader itself
// 載之器 — The vessel of loading
// ═══════════════════════════════════════════════════════════════

struct PrimitiveGeometry;

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
    /// Textures are not dropped: GPUTexture is not reference-counted, and
    /// materials hold its views and samplers directly.
    void releaseCachedGeometry();

    /// An entity drawing `arrays` as a glTF primitive would be: in its own
    /// space, under an identity transform, with the default material (white,
    /// metallic 0, roughness 0.5) and shadows cast and received. Without
    /// normals, each vertex gets the area-weighted normal of its triangles.
    /// Returns entt::null with `error` set when the arrays do not fit.
    entt::entity createMeshEntity(std::shared_ptr<ECS::Scene> scene, const std::string& name,
                                  const MeshArrays& arrays, std::string& error);

    /// Replace the geometry of an entity's MeshComponent with `arrays`; its
    /// material and transform stay. The old buffers are freed once no frame
    /// in flight draws them. Returns an error message, empty on success.
    std::string setMeshGeometry(entt::registry& registry, entt::entity entity, const MeshArrays& arrays);

private:
    /// Check `arrays` and upload them into `out`'s geometry and bounds.
    std::string buildMeshPrimitive(const MeshArrays& arrays, GltfPrimitive& out);

    VulkanDevice& m_device;

    // Per-load state (reset on each load call)
    std::filesystem::path m_basePath;
    std::filesystem::path m_currentFile;  // Scopes cache keys for embedded textures
    GltfLoadOptions m_options;

    /// Texture deduplication cache. Owns every GPUTexture it holds and spans the
    /// loader's lifetime; it is not cleared per load, because entities from an
    /// earlier load still reference these views and samplers. Freed in
    /// ~GltfSceneLoader.
    std::unordered_map<std::string, Shoonyakasha::GPUTexture> m_textureCache;

    /// Samplers for glTF samplers other than the default, keyed by their
    /// filters, wrap modes and level count. Textures that use one get a copy
    /// of the cached GPUTexture with this sampler; the cache owns both. Freed
    /// with the texture cache.
    std::unordered_map<std::string, VkSampler> m_samplerCache;

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
    /// The transform stored is the node-local one. glTF forbids shear in a
    /// node's own transform, so it round-trips exactly through
    /// TransformComponent's position/euler/scale. A product of such transforms
    /// can shear, so world matrices are composed by TransformSystem and are not
    /// decomposed here.
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

    /// Process a primitive of `mesh` (GPUBuffer + GPUTexture): standard
    /// vertices with `worldTransform` applied, or skinned vertices in mesh
    /// space, and its morph targets.
    GltfPrimitive processPrimitive(
        cgltf_data* data,
        const cgltf_mesh& mesh,
        const cgltf_primitive& primitive,
        const glm::mat4& worldTransform,
        const std::string& primitiveName,
        bool skinned
    );

    /// Build a standard-vertex GPUBuffer from a primitive's arrays; with
    /// `keepVertices`, also copies the vertex bytes there
    Shoonyakasha::GPUBuffer buildVertexBuffer(
        const PrimitiveGeometry& geometry,
        uint32_t& outVertexCount,
        uint32_t& outVertexStride,
        std::vector<uint8_t>* keepVertices = nullptr
    );

    /// Build an index GPUBuffer, 16-bit when the largest index allows it
    VkBufferUsageFlags rayTracingInputUsage() const;
    Shoonyakasha::GPUBuffer buildIndexBuffer(
        const std::vector<uint32_t>& indices,
        uint32_t& outIndexCount,
        Shoonyakasha::IndexType& outIndexType
    );

    /// Load a texture as GPUTexture, sampled as its glTF sampler says
    Shoonyakasha::GPUTexture loadTexture(
        cgltf_data* data,
        const cgltf_texture_view& textureView,
        bool srgb,
        bool normalMap = false
    );

    /// The sampler for a glTF sampler's filters and wrap modes over a
    /// texture of `mipLevels` levels; VK_NULL_HANDLE for the defaults
    /// (linear, mipmapped, repeat), which the texture's own sampler has.
    /// Made once per combination and kept in m_samplerCache.
    VkSampler gltfSampler(const cgltf_sampler* sampler, uint32_t mipLevels);

    /// Give `entity`, made from `primitive` at `node`, a MorphComponent with
    /// the mesh's default weights (or the node's), and the load's clips when
    /// it has none
    void addMorphTargets(
        const std::shared_ptr<ECS::Scene>& scene,
        entt::entity entity,
        const GltfPrimitive& primitive,
        cgltf_data* data,
        const cgltf_node* node,
        const std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>>& clips
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

    /// Load all animations from glTF data, one clip per glTF animation: the
    /// channels of `skeleton`'s joints (none without one), and with
    /// `withWeights` the morph weight channels
    std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>> loadAnimations(
        cgltf_data* data,
        const Shoonyakasha::Skeleton* skeleton,
        bool withWeights
    );

    /// Build a skinned vertex buffer: the primitive's arrays plus its joints
    /// and weights; with `keepVertices`, also copies the vertex bytes there
    Shoonyakasha::GPUBuffer buildSkinnedVertexBuffer(
        const cgltf_primitive& primitive,
        const PrimitiveGeometry& geometry,
        uint32_t& outVertexCount,
        uint32_t& outVertexStride,
        std::vector<uint8_t>* keepVertices = nullptr
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
