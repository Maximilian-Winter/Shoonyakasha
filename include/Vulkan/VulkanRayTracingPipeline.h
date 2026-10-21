//
// Shoonyakasha Engine - Vulkan Ray Tracing Pipeline
//
// A ray tracing pipeline (VK_KHR_ray_tracing_pipeline) of a ray generation
// shader, miss shaders and hit groups, with its shader binding table: one
// record per shader group, holding the group's handle and nothing else.
//

#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include <string>
#include <vector>

namespace Shoonyakasha {

class VulkanDevice;

/// The shaders of a ray tracing pipeline, as SPIR-V paths.
struct RayTracingShaders {
    struct HitGroup {
        std::string closestHit;
        std::string anyHit;
        std::string intersection;  // set: a procedural hit group
    };
    std::string           rayGen;
    std::vector<std::string> miss;
    std::vector<HitGroup> hitGroups;
    uint32_t              maxRecursionDepth = 1;
};

/// Where the records of each kind lie in a shader binding table: the ray
/// generation record, then the miss records, then the hit records, each
/// region starting on the device's base alignment and each record a handle
/// rounded up to the handle alignment.
struct ShaderBindingTableLayout {
    VkDeviceSize recordSize = 0;    // stride of the miss and hit regions
    VkDeviceSize rayGenOffset = 0, rayGenSize = 0;
    VkDeviceSize missOffset = 0, missSize = 0;
    VkDeviceSize hitOffset = 0, hitSize = 0;
    VkDeviceSize totalSize = 0;
};

ShaderBindingTableLayout layoutShaderBindingTable(uint32_t handleSize, uint32_t handleAlignment,
                                                  uint32_t baseAlignment, uint32_t missCount, uint32_t hitCount);

class VulkanRayTracingPipeline {
public:
    /// Throws when the device has no ray tracing pipelines, a shader cannot
    /// be loaded, or the recursion depth is above the device's limit.
    VulkanRayTracingPipeline(VulkanDevice& device, const RayTracingShaders& shaders,
                             const std::vector<VkDescriptorSetLayout>& descriptorLayouts,
                             const std::vector<VkPushConstantRange>& pushConstants);
    ~VulkanRayTracingPipeline();

    VulkanRayTracingPipeline(const VulkanRayTracingPipeline&) = delete;
    VulkanRayTracingPipeline& operator=(const VulkanRayTracingPipeline&) = delete;

    void bind(VkCommandBuffer cmd) const;
    /// vkCmdTraceRaysKHR over width x height x depth rays.
    void traceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height, uint32_t depth) const;

    VkPipeline getHandle() const { return m_pipeline; }
    VkPipelineLayout getLayout() const { return m_layout; }

private:
    void cleanup();

    VulkanDevice&    m_device;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkBuffer         m_table = VK_NULL_HANDLE;
    VmaAllocation    m_tableMemory = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR m_rayGenRegion{};
    VkStridedDeviceAddressRegionKHR m_missRegion{};
    VkStridedDeviceAddressRegionKHR m_hitRegion{};
    VkStridedDeviceAddressRegionKHR m_callableRegion{};
};

} // namespace Shoonyakasha
