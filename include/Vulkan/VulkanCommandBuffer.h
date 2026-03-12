//
// VulkanCommandBuffer.h - GPU command buffer management
//

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <array>

namespace Shoonyakasha {

// Forward declarations
class VulkanDevice;
class VulkanRenderPass;
class VulkanPipeline;
class VulkanDescriptorSet;
// ═══════════════════════════════════════════════════════════════
// Render State - Capturing rendering configuration
// ═══════════════════════════════════════════════════════════════

struct ViewportState {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;

    static ViewportState fromExtent(VkExtent2D extent, float minDepth = 0.0f, float maxDepth = 1.0f);
    static ViewportState fromRect(float x, float y, float width, float height);
};

struct ScissorState {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    static ScissorState fromExtent(VkExtent2D extent);
    static ScissorState fromRect(int32_t x, int32_t y, uint32_t width, uint32_t height);
    static ScissorState disabled() { return ScissorState{}; }
};

struct BlendConstants {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;

    BlendConstants() = default;
    BlendConstants(float red, float green, float blue, float alpha = 1.0f)
        : r(red), g(green), b(blue), a(alpha) {}
};

// ═══════════════════════════════════════════════════════════════
// Draw Commands - Defining what to render
// ═══════════════════════════════════════════════════════════════

struct DrawCommand {
    std::string name;
    std::shared_ptr<VulkanDescriptorSet> descriptorSet;
    uint32_t descriptorSetIndex = 0;

    // Optional overrides
    std::shared_ptr<VulkanPipeline> pipelineOverride;
    std::unordered_map<std::string, std::vector<uint8_t>> pushConstants;

    // Instancing support
    uint32_t instanceCount = 1;
    uint32_t firstInstance = 0;

    // Vertex/index buffers for drawing
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;

    explicit DrawCommand(const std::string& n) : name(n) {}

    // Fluent configuration methods
    DrawCommand& withDescriptorSet(std::shared_ptr<VulkanDescriptorSet> descriptorSet, uint32_t setIndex = 0) {
        this->descriptorSet = descriptorSet;
        this->descriptorSetIndex = setIndex;
        return *this;
    }

    DrawCommand& withPipeline(std::shared_ptr<VulkanPipeline> pipeline) {
        this->pipelineOverride = pipeline;
        return *this;
    }

    DrawCommand& withInstanceCount(uint32_t count) {
        this->instanceCount = count;
        return *this;
    }
};

struct FullscreenQuadCommand {
    std::string name;
    std::shared_ptr<VulkanDescriptorSet> descriptorSet;
    uint32_t descriptorSetIndex = 0;
    std::shared_ptr<VulkanPipeline> pipeline;

    FullscreenQuadCommand(const std::string& n, std::shared_ptr<VulkanPipeline> p)
        : name(n), pipeline(p) {}

    // Fluent configuration methods
    FullscreenQuadCommand& withDescriptorSet(std::shared_ptr<VulkanDescriptorSet> descriptorSet, uint32_t setIndex = 0) {
        this->descriptorSet = descriptorSet;
        this->descriptorSetIndex = setIndex;
        return *this;
    }
};

struct ComputeDispatchCommand {
    std::string name;
    std::shared_ptr<VulkanPipeline> computePipeline;
    std::shared_ptr<VulkanDescriptorSet> descriptorSet;
    uint32_t descriptorSetIndex = 0;
    uint32_t groupCountX = 1;
    uint32_t groupCountY = 1;
    uint32_t groupCountZ = 1;

    ComputeDispatchCommand(const std::string& n, std::shared_ptr<VulkanPipeline> p)
        : name(n), computePipeline(p) {}

    // Fluent configuration methods
    ComputeDispatchCommand& withDescriptorSet(std::shared_ptr<VulkanDescriptorSet> descriptorSet, uint32_t setIndex = 0) {
        this->descriptorSet = descriptorSet;
        this->descriptorSetIndex = setIndex;
        return *this;
    }

    ComputeDispatchCommand& withGroupCount(uint32_t x, uint32_t y = 1, uint32_t z = 1) {
        this->groupCountX = x;
        this->groupCountY = y;
        this->groupCountZ = z;
        return *this;
    }
};

// ═══════════════════════════════════════════════════════════════
// Render Pass Context - The stage upon which rendering unfolds
// ═══════════════════════════════════════════════════════════════

struct RenderPassContext {
    std::shared_ptr<VulkanRenderPass> renderPass;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    std::vector<VkClearValue> clearValues;
    uint32_t currentSubpass = 0;

    RenderPassContext() = default;
    RenderPassContext(std::shared_ptr<VulkanRenderPass> rp, VkFramebuffer fb, VkExtent2D ext)
        : renderPass(rp), framebuffer(fb), extent(ext) {}

    RenderPassContext& withClearValues(const std::vector<VkClearValue>& values);

    RenderPassContext& withDefaultClearValues();
};

// ═══════════════════════════════════════════════════════════════
// Command Buffer Builder - Fluent API for command recording
// ═══════════════════════════════════════════════════════════════

class VulkanCommandBuilder {
public:
    VulkanCommandBuilder(VulkanDevice& device, VkCommandBuffer commandBuffer);

    // Render pass management - the canvas preparation
    VulkanCommandBuilder& beginRenderPass(const RenderPassContext& context);
    VulkanCommandBuilder& nextSubpass();
    VulkanCommandBuilder& endRenderPass();

    // Pipeline binding - selecting the brush
    VulkanCommandBuilder& bindPipeline(std::shared_ptr<VulkanPipeline> pipeline);
    VulkanCommandBuilder& bindPipeline(VulkanPipeline* pipeline);

    // State management - adjusting the canvas
    VulkanCommandBuilder& setViewport(const ViewportState& viewport);
    VulkanCommandBuilder& setScissor(const ScissorState& scissor);
    VulkanCommandBuilder& setLineWidth(float width);
    VulkanCommandBuilder& setBlendConstants(const BlendConstants& constants);
    VulkanCommandBuilder& setDepthBias(float constantFactor, float clamp, float slopeFactor);

    // Resource binding - preparing the materials
    VulkanCommandBuilder& bindDescriptorSet(std::shared_ptr<VulkanDescriptorSet> descriptorSet,
                                           uint32_t setIndex = 0, uint32_t firstSet = 0);
    VulkanCommandBuilder& withDescriptorSet(std::shared_ptr<VulkanDescriptorSet> descriptorSet,
                                           uint32_t setIndex = 0, uint32_t firstSet = 0);
    VulkanCommandBuilder& bindVertexBuffers(const std::vector<VkBuffer>& buffers,
                                           const std::vector<VkDeviceSize>& offsets = {});
    VulkanCommandBuilder& bindIndexBuffer(VkBuffer buffer, VkDeviceSize offset = 0,
                                         VkIndexType indexType = VK_INDEX_TYPE_UINT32);

    // Push constants - immediate data injection
    template<typename T>
    VulkanCommandBuilder& pushConstants(VkShaderStageFlags stages, uint32_t offset, const T& data) {
        return pushConstants(stages, offset, sizeof(T), &data);
    }
    VulkanCommandBuilder& pushConstants(VkShaderStageFlags stages, uint32_t offset,
                                       uint32_t size, const void* data);

    // Drawing commands - the actual painting
    VulkanCommandBuilder& draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                              uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    VulkanCommandBuilder& drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                                     uint32_t firstInstance = 0);
    // High-level draw commands
    VulkanCommandBuilder& execute(const DrawCommand& command);
    VulkanCommandBuilder& execute(const FullscreenQuadCommand& command);
    VulkanCommandBuilder& dispatch(const ComputeDispatchCommand& command);

    // Barriers and synchronization - managing the flow of time
    VulkanCommandBuilder& memoryBarrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                       VkAccessFlags srcAccess, VkAccessFlags dstAccess);
    VulkanCommandBuilder& imageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                      VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
    VulkanCommandBuilder& bufferBarrier(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                                       VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

    // Copy operations - data transfer commands
    VulkanCommandBuilder& copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size,
                                    VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    VulkanCommandBuilder& copyBufferToImage(VkBuffer buffer, VkImage image, VkExtent3D extent,
                                           VkImageLayout imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VulkanCommandBuilder& copyImageToBuffer(VkImage image, VkBuffer buffer, VkExtent3D extent,
                                           VkImageLayout imageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Debug and profiling - observing the flow
    VulkanCommandBuilder& beginDebugLabel(const std::string& name, const std::array<float, 4> color = {1,1,1,1});
    VulkanCommandBuilder& endDebugLabel();
    VulkanCommandBuilder& insertDebugLabel(const std::string& name, const std::array<float, 4> color = {1,1,1,1});

    // Conditional execution - conditional command recording
    VulkanCommandBuilder& executeIf(bool condition, std::function<void(VulkanCommandBuilder&)> commands);
    VulkanCommandBuilder& executeUnless(bool condition, std::function<void(VulkanCommandBuilder&)> commands);

    // State queries and validation
    bool isInRenderPass() const { return m_inRenderPass; }
    bool hasPipelineBound() const { return m_currentPipeline != nullptr; }
    VulkanPipeline* getCurrentPipeline() const { return m_currentPipeline; }

    // Accessors
    VkCommandBuffer getHandle() const { return m_commandBuffer; }

private:
    VulkanDevice& m_device;
    VkCommandBuffer m_commandBuffer;

    // State tracking
    bool m_inRenderPass = false;
    VulkanPipeline* m_currentPipeline = nullptr;
    RenderPassContext m_currentRenderPassContext;

    // Debug state
    uint32_t m_debugLabelDepth = 0;

    // Helper methods
    void validateRenderPassState(const std::string& operation) const;
    void validatePipelineState(const std::string& operation) const;
};

// ═══════════════════════════════════════════════════════════════
// Command Buffer Manager - Managing command buffer lifecycle
// ═══════════════════════════════════════════════════════════════

class VulkanCommandManager {
public:
    VulkanCommandManager(VulkanDevice& device);
    ~VulkanCommandManager();

    // Command buffer creation
    VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    std::vector<VkCommandBuffer> createCommandBuffers(uint32_t count,
                                                      VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    // Single-time command execution - for simple operations
    void executeImmediate(std::function<void(VulkanCommandBuilder&)> commands);

    // Recorded command buffer management
    VkCommandBuffer beginRecording(VkCommandBufferUsageFlags usage = 0);
    VulkanCommandBuilder record(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags usage = 0);
    void endRecording(VkCommandBuffer commandBuffer);

    // Batch operations
    void submitCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers,
                             VkQueue queue,
                             const std::vector<VkSemaphore>& waitSemaphores = {},
                             const std::vector<VkPipelineStageFlags>& waitStages = {},
                             const std::vector<VkSemaphore>& signalSemaphores = {},
                             VkFence fence = VK_NULL_HANDLE);

    // Resource management
    void freeCommandBuffer(VkCommandBuffer commandBuffer);
    void freeCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers);
    void reset();

    // Convenience methods
    VulkanCommandBuilder createSingleTimeBuilder();

private:
    VulkanDevice& m_device;
    VkCommandPool m_commandPool;

    void createCommandPool();
};

} // namespace Shoonyakasha
using Shoonyakasha::ViewportState;
using Shoonyakasha::ScissorState;
using Shoonyakasha::BlendConstants;
using Shoonyakasha::DrawCommand;
using Shoonyakasha::FullscreenQuadCommand;
using Shoonyakasha::ComputeDispatchCommand;
using Shoonyakasha::RenderPassContext;
using Shoonyakasha::VulkanCommandBuilder;
using Shoonyakasha::VulkanCommandManager;

// ═══════════════════════════════════════════════════════════════
// Usage Examples - Command buffer usage patterns
// ═══════════════════════════════════════════════════════════════

/*

// Simple rendering with fluent API:
commandManager.executeImmediate([&](VulkanCommandBuilder& cmd) {
    cmd.beginRenderPass(RenderPassContext(renderPass, framebuffer, extent).withDefaultClearValues())
       .bindPipeline(materialPipeline)
       .setViewport(ViewportState::fromExtent(extent))
       .setScissor(ScissorState::fromExtent(extent))
       .withDescriptorSet(materialDescriptorSet, currentFrame)
       .drawIndexed(indexCount)
       .endRenderPass();
});

// Post-processing chain:
cmd.beginDebugLabel("Post Processing")
   .dispatch(ComputeDispatchCommand("Bloom", bloomPipeline)
             .withDescriptorSet(bloomDescriptorSet)
             .withGroupCount(extent.width / 16, extent.height / 16, 1))
   .memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
   .beginRenderPass(toneMappingContext)
   .bindPipeline(toneMappingPipeline)
   .withDescriptorSet(toneMappingDescriptorSet, currentFrame)
   .draw(3) // Fullscreen triangle
   .endRenderPass()
   .endDebugLabel();

*/
