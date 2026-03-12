//
// Shoonyakasha Engine - Vulkan Compute Pipeline
//
// 朱雀司變  熾熱而速達
// The Vermilion Bird governs transformation — blazing heat, swift arrival
// 計算之道  無形而有力
// The Way of Compute — formless yet powerful
//

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>

namespace Shoonyakasha {

class VulkanDevice;

class VulkanComputePipeline {
public:
    VulkanComputePipeline(VulkanDevice& device,
                          const std::string& computeShaderPath,
                          const std::vector<VkDescriptorSetLayout>& descriptorLayouts,
                          const std::vector<VkPushConstantRange>& pushConstants);
    ~VulkanComputePipeline();

    // Non-copyable, moveable
    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline(VulkanComputePipeline&& other) noexcept;
    VulkanComputePipeline& operator=(VulkanComputePipeline&& other) noexcept;

    /// Bind this compute pipeline to the command buffer
    void bind(VkCommandBuffer cmd);

    /// Dispatch compute work groups
    void dispatch(VkCommandBuffer cmd, uint32_t groupX, uint32_t groupY, uint32_t groupZ);

    VkPipeline getHandle() const { return m_pipeline; }
    VkPipelineLayout getLayout() const { return m_pipelineLayout; }

    /// Hot-reload: destroy and recreate from the same shader path
    void reloadShader();

private:
    VulkanDevice& m_device;
    std::string m_shaderPath;
    std::vector<VkDescriptorSetLayout> m_descriptorLayouts;
    std::vector<VkPushConstantRange> m_pushConstants;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule m_shaderModule = VK_NULL_HANDLE;

    void createPipeline();
    void createPipelineLayout();
    void cleanup();
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readShaderFile(const std::string& filename);
};

} // namespace Shoonyakasha
using Shoonyakasha::VulkanComputePipeline;
