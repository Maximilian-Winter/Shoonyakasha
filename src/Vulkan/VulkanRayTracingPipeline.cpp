//
// Shoonyakasha Engine - Vulkan Ray Tracing Pipeline Implementation
//

#include "Vulkan/VulkanRayTracingPipeline.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanMemoryAllocator.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Shoonyakasha {

namespace {

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return alignment ? (value + alignment - 1) / alignment * alignment : value;
}

VkShaderModule loadModule(VkDevice device, const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open ray tracing shader file: " + path);
    std::vector<char> code(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(code.size()));

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create a shader module from " + path);
    }
    return module;
}

} // namespace

ShaderBindingTableLayout layoutShaderBindingTable(uint32_t handleSize, uint32_t handleAlignment,
                                                  uint32_t baseAlignment, uint32_t missCount, uint32_t hitCount) {
    ShaderBindingTableLayout out;
    out.recordSize = alignUp(handleSize, handleAlignment);
    // The ray generation region's size must equal its stride, and both be
    // multiples of the base alignment.
    out.rayGenOffset = 0;
    out.rayGenSize = alignUp(out.recordSize, baseAlignment);
    out.missOffset = alignUp(out.rayGenOffset + out.rayGenSize, baseAlignment);
    out.missSize = alignUp(out.recordSize * missCount, baseAlignment);
    out.hitOffset = alignUp(out.missOffset + out.missSize, baseAlignment);
    out.hitSize = alignUp(out.recordSize * hitCount, baseAlignment);
    out.totalSize = out.hitOffset + out.hitSize;
    return out;
}

VulkanRayTracingPipeline::VulkanRayTracingPipeline(VulkanDevice& device, const RayTracingShaders& shaders,
                                                   const std::vector<VkDescriptorSetLayout>& descriptorLayouts,
                                                   const std::vector<VkPushConstantRange>& pushConstants)
    : m_device(device)
{
    if (!device.hasRayTracingPipeline()) {
        throw std::runtime_error("this device has no ray tracing pipelines");
    }
    const auto& props = device.rayTracingPipelineProperties();
    if (shaders.maxRecursionDepth > props.maxRayRecursionDepth) {
        throw std::runtime_error("maxRecursionDepth " + std::to_string(shaders.maxRecursionDepth) +
                                 " is above this device's " + std::to_string(props.maxRayRecursionDepth));
    }
    const auto& fn = device.rayTracingPipelineFunctions();
    const VkDevice vk = device.getLogicalDevice();

    std::vector<VkShaderModule> modules;
    try {
        // Stages and groups: ray generation, the miss shaders, the hit groups.
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        auto addStage = [&](VkShaderStageFlagBits stage, const std::string& path) -> uint32_t {
            if (path.empty()) return VK_SHADER_UNUSED_KHR;
            modules.push_back(loadModule(vk, path));
            VkPipelineShaderStageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            info.stage = stage;
            info.module = modules.back();
            info.pName = "main";
            stages.push_back(info);
            return static_cast<uint32_t>(stages.size() - 1);
        };
        auto group = [](VkRayTracingShaderGroupTypeKHR type) {
            VkRayTracingShaderGroupCreateInfoKHR g{};
            g.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            g.type = type;
            g.generalShader = g.closestHitShader = g.anyHitShader = g.intersectionShader = VK_SHADER_UNUSED_KHR;
            return g;
        };

        auto rayGen = group(VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR);
        rayGen.generalShader = addStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, shaders.rayGen);
        groups.push_back(rayGen);
        for (const auto& path : shaders.miss) {
            auto miss = group(VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR);
            miss.generalShader = addStage(VK_SHADER_STAGE_MISS_BIT_KHR, path);
            groups.push_back(miss);
        }
        for (const auto& hit : shaders.hitGroups) {
            auto g = group(hit.intersection.empty() ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR
                                                    : VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR);
            g.closestHitShader = addStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, hit.closestHit);
            g.anyHitShader = addStage(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, hit.anyHit);
            g.intersectionShader = addStage(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, hit.intersection);
            groups.push_back(g);
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorLayouts.size());
        layoutInfo.pSetLayouts = descriptorLayouts.empty() ? nullptr : descriptorLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
        layoutInfo.pPushConstantRanges = pushConstants.empty() ? nullptr : pushConstants.data();
        if (vkCreatePipelineLayout(vk, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create a ray tracing pipeline layout");
        }

        VkRayTracingPipelineCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        info.stageCount = static_cast<uint32_t>(stages.size());
        info.pStages = stages.data();
        info.groupCount = static_cast<uint32_t>(groups.size());
        info.pGroups = groups.data();
        info.maxPipelineRayRecursionDepth = shaders.maxRecursionDepth;
        info.layout = m_layout;
        if (fn.createPipelines(vk, VK_NULL_HANDLE, device.getPipelineCache(), 1, &info, nullptr, &m_pipeline) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create a ray tracing pipeline");
        }

        // The shader binding table: each group's handle in its record.
        const uint32_t handleSize = props.shaderGroupHandleSize;
        const auto layout = layoutShaderBindingTable(handleSize, props.shaderGroupHandleAlignment,
                                                     props.shaderGroupBaseAlignment,
                                                     static_cast<uint32_t>(shaders.miss.size()),
                                                     static_cast<uint32_t>(shaders.hitGroups.size()));
        std::vector<uint8_t> handles(static_cast<size_t>(handleSize) * groups.size());
        if (fn.getShaderGroupHandles(vk, m_pipeline, 0, static_cast<uint32_t>(groups.size()), handles.size(),
                                     handles.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to get the ray tracing shader group handles");
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = layout.totalSize;
        bufferInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocated{};
        if (vmaCreateBufferWithAlignment(device.getAllocator().getHandle(), &bufferInfo, &allocInfo,
                                         props.shaderGroupBaseAlignment, &m_table, &m_tableMemory,
                                         &allocated) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate the shader binding table");
        }
        auto* table = static_cast<uint8_t*>(allocated.pMappedData);
        std::memset(table, 0, static_cast<size_t>(layout.totalSize));
        auto handle = [&](size_t g) { return handles.data() + g * handleSize; };
        std::memcpy(table + layout.rayGenOffset, handle(0), handleSize);
        for (size_t m = 0; m < shaders.miss.size(); ++m) {
            std::memcpy(table + layout.missOffset + m * layout.recordSize, handle(1 + m), handleSize);
        }
        for (size_t h = 0; h < shaders.hitGroups.size(); ++h) {
            std::memcpy(table + layout.hitOffset + h * layout.recordSize, handle(1 + shaders.miss.size() + h),
                        handleSize);
        }
        vmaFlushAllocation(device.getAllocator().getHandle(), m_tableMemory, 0, VK_WHOLE_SIZE);

        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = m_table;
        const VkDeviceAddress base = vkGetBufferDeviceAddress(vk, &addressInfo);
        m_rayGenRegion = {base + layout.rayGenOffset, layout.rayGenSize, layout.rayGenSize};
        if (!shaders.miss.empty()) m_missRegion = {base + layout.missOffset, layout.recordSize, layout.missSize};
        if (!shaders.hitGroups.empty()) m_hitRegion = {base + layout.hitOffset, layout.recordSize, layout.hitSize};
    } catch (...) {
        for (auto module : modules) vkDestroyShaderModule(vk, module, nullptr);
        cleanup();
        throw;
    }
    for (auto module : modules) vkDestroyShaderModule(vk, module, nullptr);
}

VulkanRayTracingPipeline::~VulkanRayTracingPipeline() {
    cleanup();
}

void VulkanRayTracingPipeline::cleanup() {
    const VkDevice vk = m_device.getLogicalDevice();
    if (m_table != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_device.getAllocator().getHandle(), m_table, m_tableMemory);
        m_table = VK_NULL_HANDLE;
        m_tableMemory = VK_NULL_HANDLE;
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

void VulkanRayTracingPipeline::bind(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
}

void VulkanRayTracingPipeline::traceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                                         uint32_t depth) const {
    m_device.rayTracingPipelineFunctions().traceRays(cmd, &m_rayGenRegion, &m_missRegion, &m_hitRegion,
                                                     &m_callableRegion, width, height, depth);
}

} // namespace Shoonyakasha
