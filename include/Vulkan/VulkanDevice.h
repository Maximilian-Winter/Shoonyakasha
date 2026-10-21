//
// Created by maxim on 05.07.2024.
//

#pragma once

#include <vulkan/vulkan.h>
#include "VulkanInstance.h"
#include "VulkanMemoryAllocator.h"
#include "GPU/GpuDeleteQueue.h"
#include "Core/Logger.h"
#include "Core/EventSystem.h"
#include <vector>
#include <optional>
#include <memory>
#include <string>

namespace Shoonyakasha {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;     // Dedicated compute (prefer non-graphics)
    std::optional<uint32_t> transferFamily;    // Dedicated transfer (nice-to-have)

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanDevice {
public:
    VulkanDevice(VulkanInstance& instance, VkSurfaceKHR surface);
    ~VulkanDevice();

    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getLogicalDevice() const { return m_device; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getPresentQueue() const { return m_presentQueue; }
    VkQueue getComputeQueue() const { return m_computeQueue; }

    VkCommandPool getCommandPool() const { return m_commandPool; }
    VkCommandPool getComputeCommandPool() const { return m_computeCommandPool; }

    uint32_t getGraphicsQueueFamily() const;
    uint32_t getComputeQueueFamily() const;
    bool hasDedicatedComputeQueue() const { return m_hasDedicatedCompute; }

    /// Passes on the compute queue are submitted to it, apart from the
    /// graphics queue: the device has a compute family without graphics, and
    /// $SHOONYAKASHA_DISABLE_ASYNC_COMPUTE is not set. Without it every pass
    /// runs on the graphics queue.
    bool hasAsyncCompute() const { return m_asyncCompute; }

    /// With async compute, make the buffer or image `info` describes shared
    /// by the graphics and compute families (VK_SHARING_MODE_CONCURRENT);
    /// otherwise leave it as it is.
    void shareWithCompute(VkBufferCreateInfo& info) const;
    void shareWithCompute(VkImageCreateInfo& info) const;

    VulkanInstance& getInstance() const { return m_instance; }
    VulkanMemoryAllocator& getAllocator() const { return *m_vmaAllocator; }

    /// Shared ownership and deferred release for GPU buffers. Wrap a buffer with
    /// `adopt()` to have it freed automatically once nothing references it.
    GpuDeleteQueue& getDeleteQueue() const { return *m_deleteQueue; }

    VkMemoryPropertyFlags getMemoryProperties() const;
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    VkFormat findDepthFormat();

    /// Optional core features turned on at device creation, each only when the
    /// physical device supports it. Pipeline creation checks these before
    /// asking for depth clamping or a depth bias clamp.
    const VkPhysicalDeviceFeatures& getEnabledFeatures() const { return m_enabledFeatures; }

    /// Ray queries against acceleration structures (VK_KHR_ray_query and
    /// VK_KHR_acceleration_structure, with buffer device addresses), enabled
    /// when the device has them all. $SHOONYAKASHA_DISABLE_RAY_QUERY turns
    /// them off, to try the fallbacks.
    bool hasRayQuery() const { return m_rayQuery; }

    /// Arrays of textures indexed in shaders, with only some elements written
    /// (descriptorIndexing, runtimeDescriptorArray,
    /// shaderSampledImageArrayNonUniformIndexing and
    /// descriptorBindingPartiallyBound), enabled when the device has them all.
    /// Every device with ray queries has them.
    bool hasBindlessTextures() const { return m_bindlessTextures; }

    /// Ray tracing pipelines (VK_KHR_ray_tracing_pipeline), enabled with
    /// ray queries when the device has them; the functions they use and the
    /// device's shader group handle sizes and recursion limit.
    struct RayTracingPipelineFunctions {
        PFN_vkCreateRayTracingPipelinesKHR       createPipelines = nullptr;
        PFN_vkGetRayTracingShaderGroupHandlesKHR getShaderGroupHandles = nullptr;
        PFN_vkCmdTraceRaysKHR                    traceRays = nullptr;
    };
    bool hasRayTracingPipeline() const { return m_rayTracingPipeline; }
    const RayTracingPipelineFunctions& rayTracingPipelineFunctions() const { return m_rayTracingFunctions; }
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingPipelineProperties() const {
        return m_rayTracingProperties;
    }

    /// vkCmdDrawIndirectCount, which reads the number of draws from a buffer
    /// (the Vulkan 1.2 feature drawIndirectCount), enabled when the device
    /// has it. multiDrawIndirect and drawIndirectFirstInstance, which let one
    /// indirect call make several draws and the GPU choose firstInstance, are
    /// in getEnabledFeatures().
    bool hasDrawIndirectCount() const { return m_drawIndirectCount; }

    /// Mesh shaders (VK_EXT_mesh_shader), enabled when the device has them,
    /// and task shaders where it has those too. drawMeshTasks() records
    /// vkCmdDrawMeshTasksEXT; it is null without mesh shaders.
    bool hasMeshShader() const { return m_meshShader; }
    bool hasTaskShader() const { return m_taskShader; }
    PFN_vkCmdDrawMeshTasksEXT drawMeshTasks() const { return m_drawMeshTasks; }

    /// The physical device's limits, such as maxPushConstantsSize.
    const VkPhysicalDeviceLimits& getLimits() const { return m_limits; }

    /// Shared by every pipeline the engine creates, so recompiling a graph
    /// (on resize, for one) rebuilds its pipelines from the cache rather than
    /// from SPIR-V. Kept on disk between runs at
    /// $SHOONYAKASHA_PIPELINE_CACHE, by default shoonyakasha_pipeline_cache.bin
    /// in the working directory; "0" keeps it in memory only.
    VkPipelineCache getPipelineCache() const { return m_pipelineCache; }

    /// Write the pipeline cache to disk now. Also done when the device is
    /// destroyed.
    void savePipelineCache();
private:
    VulkanInstance& m_instance;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    VkQueue m_computeQueue;
    VkCommandPool m_commandPool;
    VkCommandPool m_computeCommandPool = VK_NULL_HANDLE;
    bool m_hasDedicatedCompute = false;
    bool m_asyncCompute = false;
    uint32_t m_sharedFamilies[2] = {0, 0};   // graphics, compute
    QueueFamilyIndices m_queueFamilyIndices;
    VkPhysicalDeviceFeatures m_enabledFeatures{};
    bool m_rayQuery = false;
    bool m_bindlessTextures = false;
    bool m_drawIndirectCount = false;
    bool m_meshShader = false;
    bool m_rayTracingPipeline = false;
    RayTracingPipelineFunctions m_rayTracingFunctions;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rayTracingProperties{};
    bool m_taskShader = false;
    PFN_vkCmdDrawMeshTasksEXT m_drawMeshTasks = nullptr;
    VkPhysicalDeviceLimits m_limits{};
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    std::string m_pipelineCachePath;
    bool supportsRayQuery(VkPhysicalDevice device) const;
    /// The mesh and task shader features of VK_EXT_mesh_shader, both false
    /// when the device lacks the extension.
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderSupport(VkPhysicalDevice device) const;
    std::unique_ptr<VulkanMemoryAllocator> m_vmaAllocator;

    // Declared after the allocator, so it is destroyed first and can still free
    // what it holds. See the lifetime note on GpuDeleteQueue.
    std::unique_ptr<GpuDeleteQueue> m_deleteQueue;

    Logger* m_logger;
    EventDispatcher* m_eventDispatcher;

    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createPipelineCache();
    bool isDeviceSuitable(VkPhysicalDevice device);

    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;
    const std::vector<const char*> m_deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

// Define the DeviceCreatedEvent
struct DeviceCreatedEvent : public Event {
    VkDevice device;
    explicit DeviceCreatedEvent(VkDevice d) : device(d) {}
};

} // namespace Shoonyakasha
using Shoonyakasha::QueueFamilyIndices;
using Shoonyakasha::SwapChainSupportDetails;
using Shoonyakasha::VulkanDevice;
using Shoonyakasha::DeviceCreatedEvent;

