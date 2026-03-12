//
// Created by maxim on 05.07.2024.
//

#pragma once
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <array>

namespace Shoonyakasha {

// Forward declarations
class VulkanDevice;

// ═══════════════════════════════════════════════════════════════
// Attachment Descriptors - Building blocks of rendering beauty
// ═══════════════════════════════════════════════════════════════

struct AttachmentDescriptor {
    std::string name;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

    // Load/Store operations - how the attachment breathes
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    // Layout transitions - the dance of image transformations
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Clear values - the void from which all rendering emerges
    VkClearValue clearValue = {};

    // Factory methods for common attachment types
    static AttachmentDescriptor createColorAttachment(
        const std::string& name,
        VkFormat format,
        const std::array<float, 4>& clearColor = {0.0f, 0.0f, 0.0f, 1.0f});

    static AttachmentDescriptor createDepthAttachment(
        const std::string& name,
        VkFormat format = VK_FORMAT_UNDEFINED, // Auto-select if undefined
        float clearDepth = 1.0f,
        uint32_t clearStencil = 0);

    static AttachmentDescriptor createResolveAttachment(
        const std::string& name,
        VkFormat format);

    static AttachmentDescriptor createInputAttachment(
        const std::string& name,
        VkFormat format);
};

// ═══════════════════════════════════════════════════════════════
// Subpass Configuration - Organizing the rendering flow
// ═══════════════════════════════════════════════════════════════

struct SubpassDescriptor {
    std::string name;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Attachment references
    std::vector<std::string> colorAttachments;
    std::vector<std::string> inputAttachments;
    std::vector<std::string> resolveAttachments;
    std::string depthStencilAttachment;
    std::vector<std::string> preserveAttachments;

    // Factory methods for common subpass patterns
    static SubpassDescriptor createBasicGraphics(
        const std::string& name,
        const std::vector<std::string>& colorTargets,
        const std::string& depthTarget = "");

    static SubpassDescriptor createMultisampled(
        const std::string& name,
        const std::vector<std::string>& colorTargets,
        const std::vector<std::string>& resolveTargets,
        const std::string& depthTarget = "");

    static SubpassDescriptor createDeferred(
        const std::string& name,
        const std::vector<std::string>& gBufferTargets,
        const std::vector<std::string>& inputTargets,
        const std::string& depthTarget);
};

// ═══════════════════════════════════════════════════════════════
// Subpass Dependencies - The flow between rendering stages
// ═══════════════════════════════════════════════════════════════

struct SubpassDependency {
    std::string srcSubpass = "VK_SUBPASS_EXTERNAL";
    std::string dstSubpass;

    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags srcAccessMask = 0;
    VkAccessFlags dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkDependencyFlags dependencyFlags = 0;

    // Factory methods for common dependency patterns
    static SubpassDependency createColorDependency(
        const std::string& src, const std::string& dst);

    static SubpassDependency createDepthDependency(
        const std::string& src, const std::string& dst);

    static SubpassDependency createExternalDependency(
        const std::string& dstSubpass);
};

// ═══════════════════════════════════════════════════════════════
// RenderPass Builder - Composing rendering flows like poetry
// ═══════════════════════════════════════════════════════════════

class RenderPassBuilder {
public:
    RenderPassBuilder();

    // Attachment configuration - defining the canvas
    RenderPassBuilder& addAttachment(const AttachmentDescriptor& attachment);
    RenderPassBuilder& addColorAttachment(const std::string& name, VkFormat format,
                                         const std::array<float, 4>& clearColor = {0.0f, 0.0f, 0.0f, 1.0f});
    RenderPassBuilder& addDepthAttachment(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED);

    // Subpass configuration - orchestrating the rendering dance
    RenderPassBuilder& addSubpass(const SubpassDescriptor& subpass);
    RenderPassBuilder& addBasicSubpass(const std::string& name,
                                      const std::vector<std::string>& colorTargets,
                                      const std::string& depthTarget = "");

    // Dependencies - the connections between moments
    RenderPassBuilder& addDependency(const SubpassDependency& dependency);
    RenderPassBuilder& addExternalDependency(const std::string& subpass);

    // Common render pass patterns
    RenderPassBuilder& configureForward(VkFormat colorFormat, VkFormat depthFormat);
    RenderPassBuilder& configureDeferred(VkFormat colorFormat, VkFormat depthFormat,
                                        const std::vector<VkFormat>& gBufferFormats);
    RenderPassBuilder& configureShadowMapping(VkFormat depthFormat);
    RenderPassBuilder& configurePostProcess(VkFormat inputFormat, VkFormat outputFormat);

    struct RenderPassConfiguration {
        std::vector<AttachmentDescriptor> attachments;
        std::vector<SubpassDescriptor> subpasses;
        std::vector<SubpassDependency> dependencies;

        // Attachment name -> index mapping
        std::unordered_map<std::string, uint32_t> attachmentIndices;
        std::unordered_map<std::string, uint32_t> subpassIndices;
    };

    RenderPassConfiguration build();

private:
    RenderPassConfiguration m_config;

    void validateConfiguration();
    void buildIndices();
};

// ═══════════════════════════════════════════════════════════════
// Modern RenderPass - Flexible like bamboo, strong like mountain
// ═══════════════════════════════════════════════════════════════

class VulkanRenderPass {
public:
    VulkanRenderPass(VulkanDevice& device, const RenderPassBuilder::RenderPassConfiguration& config);

    // Named constructors for common patterns
    static std::unique_ptr<VulkanRenderPass> createForward(
        VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat);

    static std::unique_ptr<VulkanRenderPass> createDeferred(
        VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
        const std::vector<VkFormat>& gBufferFormats);

    static std::unique_ptr<VulkanRenderPass> createShadowMap(
        VulkanDevice& device, VkFormat depthFormat);

    static std::unique_ptr<VulkanRenderPass> createPostProcess(
        VulkanDevice& device, VkFormat inputFormat, VkFormat outputFormat);

    ~VulkanRenderPass();

    // Core operations
    void begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
               VkExtent2D extent, uint32_t subpass = 0);
    void begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
               VkExtent2D extent, const std::vector<VkClearValue>& clearValues,
               uint32_t subpass = 0);

    void nextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
    void end(VkCommandBuffer commandBuffer);

    // Framebuffer creation helpers
    VkFramebuffer createFramebuffer(VkExtent2D extent, const std::vector<VkImageView>& attachments);
    VkFramebuffer createFramebuffer(VkExtent2D extent,
                                   const std::unordered_map<std::string, VkImageView>& namedAttachments);

    // Configuration introspection
    uint32_t getAttachmentIndex(const std::string& name) const;
    uint32_t getSubpassIndex(const std::string& name) const;
    const AttachmentDescriptor& getAttachment(const std::string& name) const;
    const SubpassDescriptor& getSubpass(const std::string& name) const;

    std::vector<VkClearValue> getDefaultClearValues() const;
    std::vector<VkFormat> getAttachmentFormats() const;

    // Accessors
    VkRenderPass getHandle() const { return m_renderPass; }
    const RenderPassBuilder::RenderPassConfiguration& getConfiguration() const { return m_config; }

    // Runtime modification (for dynamic render graphs)
    void updateClearValue(const std::string& attachmentName, const VkClearValue& clearValue);

private:
    VulkanDevice& m_device;
    RenderPassBuilder::RenderPassConfiguration m_config;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    void createRenderPass();
    void cleanup();

    // Helper methods
    std::vector<VkAttachmentDescription> buildAttachmentDescriptions() const;
    std::vector<VkSubpassDescription> buildSubpassDescriptions(
        std::vector<std::vector<VkAttachmentReference>>& colorRefs,
        std::vector<std::vector<VkAttachmentReference>>& inputRefs,
        std::vector<std::vector<VkAttachmentReference>>& resolveRefs,
        std::vector<VkAttachmentReference>& depthRefs) const;
    std::vector<VkSubpassDependency> buildSubpassDependencies() const;
};

} // namespace Shoonyakasha
using Shoonyakasha::AttachmentDescriptor;
using Shoonyakasha::SubpassDescriptor;
using Shoonyakasha::SubpassDependency;
using Shoonyakasha::RenderPassBuilder;
using Shoonyakasha::VulkanRenderPass;

// Usage examples:
//
// // Simple forward rendering
// auto renderPass = RenderPassBuilder()
//     .addColorAttachment("color", VK_FORMAT_B8G8R8A8_SRGB)
//     .addDepthAttachment("depth", VK_FORMAT_D32_SFLOAT)
//     .addBasicSubpass("main", {"color"}, "depth")
//     .addExternalDependency("main")
//     .build();
//
// auto modernRenderPass = std::make_unique<VulkanRenderPass>(device, renderPass);
//
// // Or use named constructors:
// auto forwardRenderPass = VulkanRenderPass::createForward(
//     device, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_D32_SFLOAT);
//
// // Deferred rendering with G-buffer
// auto deferredRenderPass = VulkanRenderPass::createDeferred(
//     device, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_D32_SFLOAT,
//     {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM});