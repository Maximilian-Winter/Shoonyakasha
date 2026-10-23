//
// RayTracingScene.h
//
// Acceleration structures of the scene, for shaders that trace rays with ray
// queries (ray-traced shadows). One bottom-level structure per distinct mesh,
// built the first time the mesh is seen; one top-level structure per frame in
// flight, rebuilt every frame from the entities that cast shadows.
//
// What goes in: entities with a mesh made for it (MeshComponent::
// rayTracingInput: static glTF meshes on a device with ray queries), that
// cast shadows, are visible and have an opaque material, as opaque
// instances. With alphaTested, alpha-tested materials go in too, as
// non-opaque instances: a ray query sees their triangles as candidates, and
// the shader decides from the texture's alpha (sk/scene_hits.glsl,
// skAlphaTestCandidate). Transparent materials and sprites stay out.
//
// Skinned meshes go in posed: every frame a compute shader
// (skin_vertices.comp, setSkinningShader) writes each one's vertices, in
// the standard layout, from its bone matrices, and its bottom-level
// structure is refitted to them. Each skinned entity has its own posed
// vertices and structure per frame in flight. Without the shader they stay
// out. Meshes with morph targets (MorphComponent) are posed from their
// blended vertices: skinned ones by the shader, the others copied as they
// are.
//
// Only exists on a device with ray queries (VulkanDevice::hasRayQuery).
//
// Alongside each top-level structure, one PackedRayInstance per instance, in
// the same order, describes the surface: a shader that hits instance i
// (rayQueryGetIntersectionInstanceIdEXT) reads row i for its vertices,
// indices and material. The rows name textures by their slots in the scene's
// SceneTextureTable.
//

#pragma once

#include "GPU/GPUTypes.h"
#include "GPU/GpuDeleteQueue.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {

class VulkanDevice;
class Logger;
struct MeshComponent;
struct MaterialComponentV5;
class SceneTextureTable;

/// One instance of the top-level structure, as shaders read it. Eight vec4s;
/// the GLSL side is python/shoonyakasha/glsl/sk/scene_hits.glsl.
struct PackedRayInstance {
    static constexpr uint32_t kNoTexture = 0xFFFFFFFFu;
    static constexpr uint32_t kIndexNone = 2;   // indexType: 0 16-bit, 1 32-bit, 2 not indexed
    static constexpr uint32_t kDoubleSided = 1;  // flags

    glm::uvec4 geometry{0};   // vertex buffer address (lo, hi), index buffer address (lo, hi)
    glm::uvec4 layout{0};     // vertex stride, index type, byte offsets (normal | uv << 8 | color << 16), flags
    glm::vec4  baseColor{1.0f};   // baseColorFactor
    glm::vec4  emissive{0.0f};    // emissiveFactor rgb, w 0
    glm::vec4  surface{0.0f};     // metallicFactor, roughnessFactor, alphaCutoff, entity id (bits)
    glm::uvec4 textures{kNoTexture};   // slots of albedoMap, normalMap, metallicRoughnessMap, emissiveMap
    glm::vec4  optics{0.0f, 1.5f, 0.0f, 1.3f};   // transmissionFactor, ior, iridescenceFactor, iridescenceIor
    // iridescenceThicknessMinimum, iridescenceThicknessMaximum (nanometres),
    // slots of iridescenceThicknessMap and iridescenceMap (bits)
    glm::vec4  film{100.0f, 400.0f, std::bit_cast<float>(kNoTexture), std::bit_cast<float>(kNoTexture)};
};
static_assert(sizeof(PackedRayInstance) == 128, "PackedRayInstance is eight vec4s");

class RayTracingScene {
public:
    RayTracingScene(VulkanDevice& device, uint32_t framesInFlight);
    ~RayTracingScene();

    RayTracingScene(const RayTracingScene&) = delete;
    RayTracingScene& operator=(const RayTracingScene&) = delete;

    /// Record this frame's top-level build into `cmd`, followed by a barrier
    /// that makes it visible to fragment and compute shaders. Builds any
    /// bottom-level structures it needs first, synchronously. Also fills this
    /// frame's instances(), with the slots `textures` gives their textures
    /// (kNoTexture for all without one). `alphaTested` puts alpha-tested
    /// materials in as non-opaque instances.
    void recordBuild(VkCommandBuffer cmd, uint32_t frameIndex, entt::registry& registry,
                     const SceneTextureTable* textures = nullptr, bool alphaTested = false);

    /// The SPIR-V file of skin_vertices.comp, which poses skinned meshes; read
    /// when the first skinned mesh is seen. Empty or unreadable: skinned
    /// meshes stay out, with a warning.
    void setSkinningShader(const std::string& spirvPath) { m_skinningShaderPath = spirvPath; }

    /// The top-level structure `recordBuild` last built for `frameIndex`.
    VkAccelerationStructureKHR topLevel(uint32_t frameIndex) const;
    uint32_t instanceCount(uint32_t frameIndex) const;
    size_t bottomLevelCount() const { return m_bottomLevels.size(); }

    /// The rows of `frameIndex`'s top-level instances, in their order.
    const std::vector<PackedRayInstance>& instances(uint32_t frameIndex) const;

    /// The row of one instance. `textureSlots` holds the slots of albedoMap,
    /// normalMap, metallicRoughnessMap and emissiveMap, `filmSlots` those of
    /// iridescenceThicknessMap and iridescenceMap (kNoTexture for none).
    /// The vertex layout is the standard mesh vertex (Vertex, VertexTypes.h):
    /// position at 0, color at 12, uv at 24, normal at 32; the stride is the mesh's.
    static PackedRayInstance packInstance(const MaterialComponentV5& material, uint32_t entity,
                                          const MeshComponent& mesh, VkDeviceAddress vertices,
                                          VkDeviceAddress indices, const glm::uvec4& textureSlots,
                                          const glm::uvec2& filmSlots = glm::uvec2(PackedRayInstance::kNoTexture));

    /// The material texture slots a row names, in PackedRayInstance::textures order.
    static constexpr const char* kTextureNames[4] = {"albedoMap", "normalMap", "metallicRoughnessMap",
                                                     "emissiveMap"};
    /// The film's texture slots, in PackedRayInstance::film.zw order.
    static constexpr const char* kFilmTextureNames[2] = {"iridescenceThicknessMap", "iridescenceMap"};

private:
    struct AccelerationStructure {
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        GPUBuffer buffer;
        VkDeviceAddress address = 0;
    };
    struct BottomLevel {
        AccelerationStructure structure;
        std::weak_ptr<const GPUBuffer> vertices, indices;   // expired: the mesh is gone
    };
    struct TopLevel {
        AccelerationStructure structure;
        GPUBuffer scratch;
        GPUBuffer instances;           // host-visible VkAccelerationStructureInstanceKHR array
        void* mapped = nullptr;
        uint32_t capacity = 0;
        uint32_t count = 0;
        std::vector<PackedRayInstance> rows;
    };
    struct MeshKey {
        VkBuffer vertices, indices;
        uint32_t vertexCount, indexCount;
        bool operator==(const MeshKey& o) const {
            return vertices == o.vertices && indices == o.indices &&
                   vertexCount == o.vertexCount && indexCount == o.indexCount;
        }
    };
    struct MeshKeyHash {
        size_t operator()(const MeshKey& k) const {
            return std::hash<const void*>()(k.vertices) ^ (std::hash<const void*>()(k.indices) << 1) ^
                   (static_cast<size_t>(k.vertexCount) << 7) ^ (static_cast<size_t>(k.indexCount) << 13);
        }
    };

    /// A skinned entity's posed copy for one frame in flight.
    struct PosedMesh {
        GPUBuffer bones;                 // host-visible bone matrices
        void* bonesMapped = nullptr;
        uint32_t jointCount = 0;
        GPUBuffer vertices;              // standard vertices, written by the skinning shader or copied
        uint32_t vertexCount = 0;
        VkBuffer source = VK_NULL_HANDLE;   // the vertex buffer it poses
        AccelerationStructure structure; // built once, refitted every frame after
        GPUBuffer scratch;
        bool built = false;
    };
    struct PosedEntity {
        std::vector<PosedMesh> frames;   // one per frame in flight
        uint64_t lastBuild = 0;
    };

    const BottomLevel* bottomLevelFor(const MeshComponent& mesh);
    bool skinningReady();
    void destroyPosed(PosedMesh& posed);
    /// The entity's posed copy for `frame`, posed from `source`: skinned
    /// vertices with `jointCount` joints, or standard vertices copied as
    /// they are for 0.
    PosedMesh& posedFor(entt::entity entity, uint32_t frame, const MeshComponent& mesh, uint32_t jointCount,
                        VkBuffer source);
    void recordPosedBuilds(VkCommandBuffer cmd, const std::vector<PosedMesh*>& posed,
                           const std::vector<const MeshComponent*>& meshes);
    void releaseExpired();
    void ensureTopLevelCapacity(TopLevel& top, uint32_t count);
    AccelerationStructure createStructure(VkAccelerationStructureTypeKHR type, VkDeviceSize size);
    void destroyStructure(AccelerationStructure& s);
    GPUBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible,
                           VkDeviceSize alignment = 0);
    VkDeviceAddress addressOf(VkBuffer buffer) const;

    VulkanDevice& m_device;
    Logger* m_logger = nullptr;
    uint32_t m_scratchAlignment = 256;
    std::unordered_map<MeshKey, BottomLevel, MeshKeyHash> m_bottomLevels;
    std::vector<TopLevel> m_topLevels;
    // Bottom levels whose meshes are gone, destroyed once no frame in flight
    // can still reference them: (structure, builds remaining).
    std::vector<std::pair<AccelerationStructure, uint32_t>> m_retiring;
    uint32_t m_framesInFlight;

    std::unordered_map<entt::entity, PosedEntity> m_posed;
    // Posed entities no longer drawn, destroyed once no frame in flight can
    // still reference them: (entity's copies, builds remaining).
    std::vector<std::pair<PosedEntity, uint32_t>> m_retiringPosed;
    uint64_t m_buildCount = 0;
    std::string m_skinningShaderPath;
    bool m_skinningTried = false;
    VkPipelineLayout m_skinLayout = VK_NULL_HANDLE;
    VkPipeline m_skinPipeline = VK_NULL_HANDLE;

    PFN_vkCreateAccelerationStructureKHR m_create = nullptr;
    PFN_vkDestroyAccelerationStructureKHR m_destroy = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR m_buildSizes = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR m_cmdBuild = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_deviceAddress = nullptr;
};

} // namespace Shoonyakasha
