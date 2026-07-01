//
// Created by maxim on 05.07.2024.
//

#pragma once

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <optional>

#include "VertexTypes.h"

namespace Shoonyakasha {

class VulkanPipeline;
// Forward declarations
class VulkanDevice;
class VulkanRenderPass;

// ═══════════════════════════════════════════════════════════════
// Pipeline State Builder - Fluent API for pipeline configuration
// ═══════════════════════════════════════════════════════════════

class PipelineStateBuilder {
public:
    PipelineStateBuilder();

    // Shader configuration
    PipelineStateBuilder& withShaders(const std::string& vertPath, const std::string& fragPath);
    PipelineStateBuilder& withComputeShader(const std::string& computePath);
    PipelineStateBuilder& withGeometryShader(const std::string& geomPath);

    // Input assembly configuration
    PipelineStateBuilder& withTopology(VkPrimitiveTopology topology);
    PipelineStateBuilder& withPrimitiveRestart(bool enable = true);

    // Vertex input configuration
    template<typename VertexType>
    PipelineStateBuilder& withVertexType();
    PipelineStateBuilder& withCustomVertexInput(
        const std::vector<VkVertexInputBindingDescription>& bindings,
        const std::vector<VkVertexInputAttributeDescription>& attributes);

    // Rasterization state configuration
    PipelineStateBuilder& withWireframe(bool enable = true);
    PipelineStateBuilder& withCulling(VkCullModeFlags cullMode, VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE);
    PipelineStateBuilder& withDepthClamp(bool enable = true);
    PipelineStateBuilder& withLineWidth(float width);

    // Depth and stencil test configuration
    PipelineStateBuilder& withDepthTest(bool enable = true, VkCompareOp compareOp = VK_COMPARE_OP_LESS);
    PipelineStateBuilder& withDepthWrite(bool enable = true);
    PipelineStateBuilder& withDepthBounds(float min, float max);
    PipelineStateBuilder& withStencilTest(bool enable = true);

    // Color blending configuration
    PipelineStateBuilder& withBlending(bool enable = true);
    PipelineStateBuilder& withAlphaBlending();
    PipelineStateBuilder& withAdditiveBlending();
    PipelineStateBuilder& withCustomBlending(VkBlendFactor srcColor, VkBlendFactor dstColor, VkBlendOp op);
    // Full control over both color and alpha blend factors/ops (e.g. for
    // premultiplied alpha, where color and alpha factors differ).
    PipelineStateBuilder& withCustomBlending(VkBlendFactor srcColor, VkBlendFactor dstColor, VkBlendOp colorOp,
                                              VkBlendFactor srcAlpha, VkBlendFactor dstAlpha, VkBlendOp alphaOp);
    PipelineStateBuilder& withColorAttachmentCount(uint32_t count);

    // Multisampling configuration
    PipelineStateBuilder& withMultisampling(VkSampleCountFlagBits samples, bool sampleShading = false);

    // Dynamic state configuration
    PipelineStateBuilder& withDynamicViewport();
    PipelineStateBuilder& withDynamicScissor();
    PipelineStateBuilder& withDynamicLineWidth();
    PipelineStateBuilder& withDynamicState(VkDynamicState state);

    // Resource binding configuration
    PipelineStateBuilder& withDescriptorSetLayout(VkDescriptorSetLayout layout);
    PipelineStateBuilder& withPushConstants(VkShaderStageFlags stages, uint32_t size, uint32_t offset = 0);
    std::unique_ptr<VulkanPipeline> buildPipeline(VulkanDevice& device,
                                                         VulkanRenderPass& renderPass,
                                                         VkExtent2D extent);

    // Build the immutable state
    struct PipelineState {
        // Shader stages
        std::vector<std::pair<VkShaderStageFlagBits, std::string>> shaderPaths;

        // Input assembly
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool primitiveRestart = false;

        // Vertex input
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;

        // Rasterization
        bool wireframe = false;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        bool depthClamp = false;
        float lineWidth = 1.0f;

        // Depth/Stencil
        bool depthTest = true;
        bool depthWrite = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        bool depthBounds = false;
        float minDepthBounds = 0.0f;
        float maxDepthBounds = 1.0f;
        bool stencilTest = false;

        // Blending
        bool blending = false;
        VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
        VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;

        // Color attachment count (for MRT; 0 = auto-detect as 1)
        uint32_t colorAttachmentCount = 0;

        // Multisampling
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
        bool sampleShading = false;
        float minSampleShading = 1.0f;

        // Dynamic states
        std::vector<VkDynamicState> dynamicStates;

        // Resource binding
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        std::vector<VkPushConstantRange> pushConstantRanges;
    };

    PipelineState build() const;

private:
    PipelineState m_state;
};

// ═══════════════════════════════════════════════════════════════
// Modern Pipeline - Flexible graphics pipeline implementation
// ═══════════════════════════════════════════════════════════════

class VulkanPipeline {
public:
    // Constructor with builder pattern for flexible configuration
    VulkanPipeline(VulkanDevice& device,
                   VulkanRenderPass& renderPass,
                   VkExtent2D extent,
                   const PipelineStateBuilder::PipelineState& state);

    // Named constructors for common patterns
    static std::unique_ptr<VulkanPipeline> createDefault(
        VulkanDevice& device,
        VulkanRenderPass& renderPass,
        VkExtent2D extent,
        const std::string& vertShader,
        const std::string& fragShader);

    static std::unique_ptr<VulkanPipeline> createWireframe(
        VulkanDevice& device,
        VulkanRenderPass& renderPass,
        VkExtent2D extent,
        const std::string& vertShader,
        const std::string& fragShader);

    static std::unique_ptr<VulkanPipeline> createTransparent(
        VulkanDevice& device,
        VulkanRenderPass& renderPass,
        VkExtent2D extent,
        const std::string& vertShader,
        const std::string& fragShader);

    ~VulkanPipeline();

    // Core operations
    void bind(VkCommandBuffer commandBuffer);
    void recreate(VkExtent2D newExtent);

    // Hot-reloading for development workflow
    void reloadShaders();

    // State introspection
    bool supportsWireframe() const;
    bool hasTransparency() const;
    const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() const;

    // Accessors
    VkPipeline getHandle() const { return m_pipeline; }
    VkPipelineLayout getLayout() const { return m_pipelineLayout; }
    const PipelineStateBuilder::PipelineState& getState() const { return m_state; }

private:
    VulkanDevice& m_device;
    VulkanRenderPass& m_renderPass;
    VkExtent2D m_extent;
    PipelineStateBuilder::PipelineState m_state;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Cached shader modules for hot-reloading
    std::unordered_map<VkShaderStageFlagBits, VkShaderModule> m_shaderModules;

    void createPipeline();
    void createPipelineLayout();
    void loadShaders();
    void cleanup();

    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readShaderFile(const std::string& filename);
};

// ═══════════════════════════════════════════════════════════════
// Template specializations for common vertex types
// ═══════════════════════════════════════════════════════════════

template<>
PipelineStateBuilder& PipelineStateBuilder::withVertexType<Vertex>();

} // namespace Shoonyakasha
using Shoonyakasha::PipelineStateBuilder;
using Shoonyakasha::VulkanPipeline;

// Usage examples:
//
// auto pipeline = PipelineStateBuilder()
//     .withShaders("basic.vert", "basic.frag")
//     .withVertexType<Vertex>()
//     .withDepthTest(true)
//     .withCulling(VK_CULL_MODE_BACK_BIT)
//     .withAlphaBlending()
//     .build();
//
// auto modernPipeline = std::make_unique<VulkanPipeline>(
//     device, renderPass, extent, pipeline);
//
// // Or use named constructors for common patterns:
// auto defaultPipeline = VulkanPipeline::createDefault(
//     device, renderPass, extent, "vertex.spv", "fragment.spv");
