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

#pragma once

#include "GPU/GPUTypes.h"
#include "GPU/GpuDeleteQueue.h"

#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {

class VulkanDevice;
class Logger;
struct MeshComponent;

class RayTracingScene {
public:
    RayTracingScene(VulkanDevice& device, uint32_t framesInFlight);
    ~RayTracingScene();

    RayTracingScene(const RayTracingScene&) = delete;
    RayTracingScene& operator=(const RayTracingScene&) = delete;

    /// Record this frame's top-level build into `cmd`, followed by a barrier
    /// that makes it visible to fragment and compute shaders. Builds any
    /// bottom-level structures it needs first, synchronously.
    void recordBuild(VkCommandBuffer cmd, uint32_t frameIndex, entt::registry& registry);

    /// The top-level structure `recordBuild` last built for `frameIndex`.
    VkAccelerationStructureKHR topLevel(uint32_t frameIndex) const;
    uint32_t instanceCount(uint32_t frameIndex) const;
    size_t bottomLevelCount() const { return m_bottomLevels.size(); }

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
