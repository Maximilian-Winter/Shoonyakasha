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
// cast shadows, are visible and have an opaque material. Skinned meshes,
// alpha-tested and transparent materials and sprites stay out; a pipeline
// shadows those some other way (the default pipeline: its shadow maps).
//
// Only exists on a device with ray queries (VulkanDevice::hasRayQuery).
//
// Alongside each top-level structure, one PackedRayInstance per instance, in
// the same order, describes the surface: a shader that hits instance i
// (rayQueryGetIntersectionInstanceIdEXT) reads row i for its vertices,
// indices and material. The textures the rows name are listed in slots, for
// a pipeline's array of the scene's textures.
//

#pragma once

#include "GPU/GPUTypes.h"
#include "GPU/GpuDeleteQueue.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {

class VulkanDevice;
class Logger;
struct MeshComponent;
struct MaterialComponentV5;

/// One instance of the top-level structure, as shaders read it. Six vec4s;
/// the GLSL side is python/shoonyakasha/glsl/scene_hits.glsl.
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
};
static_assert(sizeof(PackedRayInstance) == 96, "PackedRayInstance is six vec4s");

class RayTracingScene {
public:
    /// A texture the instances use, at the slot that is its index in textures().
    struct TextureSlot {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        bool operator==(const TextureSlot& o) const { return view == o.view && sampler == o.sampler; }
    };

    RayTracingScene(VulkanDevice& device, uint32_t framesInFlight);
    ~RayTracingScene();

    RayTracingScene(const RayTracingScene&) = delete;
    RayTracingScene& operator=(const RayTracingScene&) = delete;

    /// Record this frame's top-level build into `cmd`, followed by a barrier
    /// that makes it visible to fragment and compute shaders. Builds any
    /// bottom-level structures it needs first, synchronously. Also fills this
    /// frame's instances() and textures(), giving at most `textureCapacity`
    /// textures a slot; the rest read as kNoTexture.
    void recordBuild(VkCommandBuffer cmd, uint32_t frameIndex, entt::registry& registry,
                     uint32_t textureCapacity = 0);

    /// The top-level structure `recordBuild` last built for `frameIndex`.
    VkAccelerationStructureKHR topLevel(uint32_t frameIndex) const;
    uint32_t instanceCount(uint32_t frameIndex) const;
    size_t bottomLevelCount() const { return m_bottomLevels.size(); }

    /// The rows of `frameIndex`'s top-level instances, in their order.
    const std::vector<PackedRayInstance>& instances(uint32_t frameIndex) const;
    /// The textures those rows name, by slot.
    const std::vector<TextureSlot>& textures(uint32_t frameIndex) const;

    /// The row of one instance. `textureSlots` holds the slots of albedoMap,
    /// normalMap, metallicRoughnessMap and emissiveMap (kNoTexture for none).
    /// The vertex layout is the 44-byte glTF vertex every mesh in the
    /// structure has: position at 0, color at 12, uv at 24, normal at 32.
    static PackedRayInstance packInstance(const MaterialComponentV5& material, uint32_t entity,
                                          const MeshComponent& mesh, VkDeviceAddress vertices,
                                          VkDeviceAddress indices, const glm::uvec4& textureSlots);

    /// The material texture slots a row names, in PackedRayInstance::textures order.
    static constexpr const char* kTextureNames[4] = {"albedoMap", "normalMap", "metallicRoughnessMap",
                                                     "emissiveMap"};

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
        std::vector<TextureSlot> textures;
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

    const BottomLevel* bottomLevelFor(const MeshComponent& mesh);
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

    PFN_vkCreateAccelerationStructureKHR m_create = nullptr;
    PFN_vkDestroyAccelerationStructureKHR m_destroy = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR m_buildSizes = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR m_cmdBuild = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_deviceAddress = nullptr;
};

} // namespace Shoonyakasha
