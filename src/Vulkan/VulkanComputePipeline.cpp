//
// Shoonyakasha Engine - Vulkan Compute Pipeline Implementation
//
// 朱雀之焰煉其精純
// The Vermilion Bird's flame refines to purity
//

#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanDevice.h"

#include <fstream>
#include <stdexcept>

namespace Shoonyakasha {

VulkanComputePipeline::VulkanComputePipeline(
    VulkanDevice& device,
    const std::string& computeShaderPath,
    const std::vector<VkDescriptorSetLayout>& descriptorLayouts,
    const std::vector<VkPushConstantRange>& pushConstants)
    : m_device(device)
    , m_shaderPath(computeShaderPath)
    , m_descriptorLayouts(descriptorLayouts)
    , m_pushConstants(pushConstants)
{
    createPipelineLayout();
    createPipeline();
}

VulkanComputePipeline::~VulkanComputePipeline() {
    cleanup();
}

VulkanComputePipeline::VulkanComputePipeline(VulkanComputePipeline&& other) noexcept
    : m_device(other.m_device)
    , m_shaderPath(std::move(other.m_shaderPath))
    , m_descriptorLayouts(std::move(other.m_descriptorLayouts))
    , m_pushConstants(std::move(other.m_pushConstants))
    , m_pipeline(other.m_pipeline)
    , m_pipelineLayout(other.m_pipelineLayout)
    , m_shaderModule(other.m_shaderModule)
{
    other.m_pipeline = VK_NULL_HANDLE;
    other.m_pipelineLayout = VK_NULL_HANDLE;
    other.m_shaderModule = VK_NULL_HANDLE;
}

VulkanComputePipeline& VulkanComputePipeline::operator=(VulkanComputePipeline&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_shaderPath = std::move(other.m_shaderPath);
        m_descriptorLayouts = std::move(other.m_descriptorLayouts);
        m_pushConstants = std::move(other.m_pushConstants);
        m_pipeline = other.m_pipeline;
        m_pipelineLayout = other.m_pipelineLayout;
        m_shaderModule = other.m_shaderModule;
        other.m_pipeline = VK_NULL_HANDLE;
        other.m_pipelineLayout = VK_NULL_HANDLE;
        other.m_shaderModule = VK_NULL_HANDLE;
    }
    return *this;
}

void VulkanComputePipeline::bind(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
}

void VulkanComputePipeline::dispatch(VkCommandBuffer cmd, uint32_t groupX, uint32_t groupY, uint32_t groupZ) {
    vkCmdDispatch(cmd, groupX, groupY, groupZ);
}

void VulkanComputePipeline::reloadShader() {
    // Destroy old pipeline but keep layout
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device.getLogicalDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_shaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device.getLogicalDevice(), m_shaderModule, nullptr);
        m_shaderModule = VK_NULL_HANDLE;
    }
    createPipeline();
}

void VulkanComputePipeline::createPipelineLayout() {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorLayouts.size());
    layoutInfo.pSetLayouts = m_descriptorLayouts.empty() ? nullptr : m_descriptorLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(m_pushConstants.size());
    layoutInfo.pPushConstantRanges = m_pushConstants.empty() ? nullptr : m_pushConstants.data();

    if (vkCreatePipelineLayout(m_device.getLogicalDevice(), &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline layout!");
    }
}

void VulkanComputePipeline::createPipeline() {
    auto shaderCode = readShaderFile(m_shaderPath);
    m_shaderModule = createShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = m_shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_pipelineLayout;

    if (vkCreateComputePipelines(m_device.getLogicalDevice(), VK_NULL_HANDLE,
                                  1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline!");
    }
}

void VulkanComputePipeline::cleanup() {
    VkDevice device = m_device.getLogicalDevice();

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_shaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, m_shaderModule, nullptr);
        m_shaderModule = VK_NULL_HANDLE;
    }
}

VkShaderModule VulkanComputePipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device.getLogicalDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute shader module!");
    }
    return shaderModule;
}

std::vector<char> VulkanComputePipeline::readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open compute shader file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

} // namespace Shoonyakasha
