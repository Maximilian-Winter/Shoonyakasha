//
// VulkanCommandBuffer.cpp - Implementation of command buffer recording
//

#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanRenderPass.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanDescriptorSystem.h"

#include <stdexcept>
#include <cstring>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// ViewportState Implementation
// ═══════════════════════════════════════════════════════════════

ViewportState ViewportState::fromExtent(VkExtent2D extent, float minDepth, float maxDepth) {
    ViewportState viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = minDepth;
    viewport.maxDepth = maxDepth;
    return viewport;
}

ViewportState ViewportState::fromRect(float x, float y, float width, float height) {
    ViewportState viewport;
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    return viewport;
}

// ═══════════════════════════════════════════════════════════════
// ScissorState Implementation
// ═══════════════════════════════════════════════════════════════

ScissorState ScissorState::fromExtent(VkExtent2D extent) {
    ScissorState scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = extent.width;
    scissor.height = extent.height;
    return scissor;
}

ScissorState ScissorState::fromRect(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    ScissorState scissor;
    scissor.x = x;
    scissor.y = y;
    scissor.width = width;
    scissor.height = height;
    return scissor;
}

// ═══════════════════════════════════════════════════════════════
// VulkanCommandBuilder Implementation
// ═══════════════════════════════════════════════════════════════

RenderPassContext & RenderPassContext::withClearValues(const std::vector<VkClearValue> &values) {
    clearValues = values;
    return *this;
}

RenderPassContext & RenderPassContext::withDefaultClearValues() {
    if (renderPass) {
        clearValues = renderPass->getDefaultClearValues();
    }
    return *this;
}

VulkanCommandBuilder::VulkanCommandBuilder(VulkanDevice& device, VkCommandBuffer commandBuffer)
    : m_device(device), m_commandBuffer(commandBuffer) {
    // Ready for command recording
}

VulkanCommandBuilder& VulkanCommandBuilder::beginRenderPass(const RenderPassContext& context) {
    if (m_inRenderPass) {
        throw std::runtime_error("Already inside a render pass");
    }

    m_currentRenderPassContext = context;
    m_inRenderPass = true;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = context.renderPass->getHandle();
    renderPassInfo.framebuffer = context.framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = context.extent;

    auto clearValues = context.clearValues.empty() ?
        context.renderPass->getDefaultClearValues() : context.clearValues;

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(m_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::nextSubpass() {
    validateRenderPassState("nextSubpass");
    vkCmdNextSubpass(m_commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
    m_currentRenderPassContext.currentSubpass++;
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::endRenderPass() {
    validateRenderPassState("endRenderPass");
    vkCmdEndRenderPass(m_commandBuffer);
    m_inRenderPass = false;
    m_currentPipeline = nullptr; // Pipeline binding is render pass specific
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::bindPipeline(std::shared_ptr<VulkanPipeline> pipeline) {
    return bindPipeline(pipeline.get());
}

VulkanCommandBuilder& VulkanCommandBuilder::bindPipeline(VulkanPipeline* pipeline) {
    if (!pipeline) {
        throw std::runtime_error("Cannot bind null pipeline");
    }

    pipeline->bind(m_commandBuffer);
    m_currentPipeline = pipeline;
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::setViewport(const ViewportState& viewport) {
    VkViewport vkViewport{};
    vkViewport.x = viewport.x;
    vkViewport.y = viewport.y;
    vkViewport.width = viewport.width;
    vkViewport.height = viewport.height;
    vkViewport.minDepth = viewport.minDepth;
    vkViewport.maxDepth = viewport.maxDepth;

    vkCmdSetViewport(m_commandBuffer, 0, 1, &vkViewport);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::setScissor(const ScissorState& scissor) {
    VkRect2D vkScissor{};
    vkScissor.offset.x = scissor.x;
    vkScissor.offset.y = scissor.y;
    vkScissor.extent.width = scissor.width;
    vkScissor.extent.height = scissor.height;

    vkCmdSetScissor(m_commandBuffer, 0, 1, &vkScissor);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::setLineWidth(float width) {
    vkCmdSetLineWidth(m_commandBuffer, width);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::setBlendConstants(const BlendConstants& constants) {
    float blendConstants[4] = {constants.r, constants.g, constants.b, constants.a};
    vkCmdSetBlendConstants(m_commandBuffer, blendConstants);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::setDepthBias(float constantFactor, float clamp, float slopeFactor) {
    vkCmdSetDepthBias(m_commandBuffer, constantFactor, clamp, slopeFactor);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::bindDescriptorSet(std::shared_ptr<VulkanDescriptorSet> descriptorSet,
                                                             uint32_t setIndex, uint32_t firstSet) {
    validatePipelineState("bindDescriptorSet");
    descriptorSet->bind(m_commandBuffer, m_currentPipeline->getLayout(), setIndex, firstSet);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::withDescriptorSet(std::shared_ptr<VulkanDescriptorSet> descriptorSet,
                                                             uint32_t setIndex, uint32_t firstSet) {
    // Alias for bindDescriptorSet - provides more fluent naming option
    return bindDescriptorSet(descriptorSet, setIndex, firstSet);
}

VulkanCommandBuilder& VulkanCommandBuilder::bindVertexBuffers(const std::vector<VkBuffer>& buffers,
                                                              const std::vector<VkDeviceSize>& offsets) {
    std::vector<VkDeviceSize> actualOffsets = offsets;
    if (actualOffsets.empty()) {
        actualOffsets.resize(buffers.size(), 0);
    }

    vkCmdBindVertexBuffers(m_commandBuffer, 0, static_cast<uint32_t>(buffers.size()),
                          buffers.data(), actualOffsets.data());
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::bindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    vkCmdBindIndexBuffer(m_commandBuffer, buffer, offset, indexType);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::pushConstants(VkShaderStageFlags stages, uint32_t offset,
                                                          uint32_t size, const void* data) {
    validatePipelineState("pushConstants");
    vkCmdPushConstants(m_commandBuffer, m_currentPipeline->getLayout(), stages, offset, size, data);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::draw(uint32_t vertexCount, uint32_t instanceCount,
                                                 uint32_t firstVertex, uint32_t firstInstance) {
    validateRenderPassState("draw");
    validatePipelineState("draw");
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                                        uint32_t firstIndex, int32_t vertexOffset,
                                                        uint32_t firstInstance) {
    validateRenderPassState("drawIndexed");
    validatePipelineState("drawIndexed");
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::execute(const DrawCommand& command) {
    if (command.pipelineOverride) {
        bindPipeline(command.pipelineOverride);
    }

    if (command.descriptorSet) {
        bindDescriptorSet(command.descriptorSet, command.descriptorSetIndex);
    }

    // Bind vertex/index buffers and draw
    if (command.vertexBuffer != VK_NULL_HANDLE) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &command.vertexBuffer, &offset);
    }

    if (command.indexBuffer != VK_NULL_HANDLE && command.indexCount > 0) {
        vkCmdBindIndexBuffer(m_commandBuffer, command.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        return drawIndexed(command.indexCount, command.instanceCount);
    } else if (command.vertexCount > 0) {
        return draw(command.vertexCount, command.instanceCount);
    }

    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::execute(const FullscreenQuadCommand& command) {
    bindPipeline(command.pipeline);

    if (command.descriptorSet) {
        bindDescriptorSet(command.descriptorSet, command.descriptorSetIndex);
    }

    // Draw fullscreen quad (3 vertices for a triangle that covers the screen)
    return draw(3);
}

VulkanCommandBuilder& VulkanCommandBuilder::dispatch(const ComputeDispatchCommand& command) {
    if (m_inRenderPass) {
        throw std::runtime_error("Cannot dispatch compute inside render pass");
    }

    bindPipeline(command.computePipeline);

    if (command.descriptorSet) {
        bindDescriptorSet(command.descriptorSet, command.descriptorSetIndex);
    }

    vkCmdDispatch(m_commandBuffer, command.groupCountX, command.groupCountY, command.groupCountZ);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::memoryBarrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                                          VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(m_commandBuffer, srcStage, dstStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::imageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                                         VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // Set access masks based on layouts (simplified version)
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }

    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }

    vkCmdPipelineBarrier(m_commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::bufferBarrier(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                                                          VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = offset;
    barrier.size = size;

    vkCmdPipelineBarrier(m_commandBuffer, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size,
                                                       VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;

    vkCmdCopyBuffer(m_commandBuffer, src, dst, 1, &copyRegion);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::copyBufferToImage(VkBuffer buffer, VkImage image, VkExtent3D extent,
                                                              VkImageLayout imageLayout) {
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = extent;

    vkCmdCopyBufferToImage(m_commandBuffer, buffer, image, imageLayout, 1, &region);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::copyImageToBuffer(VkImage image, VkBuffer buffer, VkExtent3D extent,
                                                              VkImageLayout imageLayout) {
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = extent;

    vkCmdCopyImageToBuffer(m_commandBuffer, image, imageLayout, buffer,  1, &region);
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::beginDebugLabel(const std::string& name, const std::array<float, 4> color) {
    // Debug labels require VK_EXT_debug_utils extension
    auto func = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(m_device.getLogicalDevice(), "vkCmdBeginDebugUtilsLabelEXT");
    if (func) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name.c_str();
        label.color[0] = color[0];
        label.color[1] = color[1];
        label.color[2] = color[2];
        label.color[3] = color[3];
        func(m_commandBuffer, &label);
        m_debugLabelDepth++;
    }
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::endDebugLabel() {
    auto func = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(m_device.getLogicalDevice(), "vkCmdEndDebugUtilsLabelEXT");
    if (func && m_debugLabelDepth > 0) {
        func(m_commandBuffer);
        m_debugLabelDepth--;
    }
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::insertDebugLabel(const std::string& name, const std::array<float, 4> color) {
    auto func = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetDeviceProcAddr(m_device.getLogicalDevice(), "vkCmdInsertDebugUtilsLabelEXT");
    if (func) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name.c_str();
        label.color[0] = color[0];
        label.color[1] = color[1];
        label.color[2] = color[2];
        label.color[3] = color[3];
        func(m_commandBuffer, &label);
    }
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::executeIf(bool condition, std::function<void(VulkanCommandBuilder&)> commands) {
    if (condition) {
        commands(*this);
    }
    return *this;
}

VulkanCommandBuilder& VulkanCommandBuilder::executeUnless(bool condition, std::function<void(VulkanCommandBuilder&)> commands) {
    return executeIf(!condition, commands);
}

void VulkanCommandBuilder::validateRenderPassState(const std::string& operation) const {
    if (!m_inRenderPass) {
        throw std::runtime_error("Operation '" + operation + "' requires an active render pass");
    }
}

void VulkanCommandBuilder::validatePipelineState(const std::string& operation) const {
    if (!m_currentPipeline) {
        throw std::runtime_error("Operation '" + operation + "' requires a bound pipeline");
    }
}

// ═══════════════════════════════════════════════════════════════
// VulkanCommandManager Implementation - The conductor
// ═══════════════════════════════════════════════════════════════

VulkanCommandManager::VulkanCommandManager(VulkanDevice& device) : m_device(device) {
    createCommandPool();
}

VulkanCommandManager::~VulkanCommandManager() {
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device.getLogicalDevice(), m_commandPool, nullptr);
    }
}

void VulkanCommandManager::createCommandPool() {
    auto queueFamilyIndices = m_device.findQueueFamilies(m_device.getPhysicalDevice());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(m_device.getLogicalDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool!");
    }
}

VkCommandBuffer VulkanCommandManager::createCommandBuffer(VkCommandBufferLevel level) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = level;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(m_device.getLogicalDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffer!");
    }

    return commandBuffer;
}

std::vector<VkCommandBuffer> VulkanCommandManager::createCommandBuffers(uint32_t count, VkCommandBufferLevel level) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = level;
    allocInfo.commandBufferCount = count;

    std::vector<VkCommandBuffer> commandBuffers(count);
    if (vkAllocateCommandBuffers(m_device.getLogicalDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers!");
    }

    return commandBuffers;
}

void VulkanCommandManager::executeImmediate(std::function<void(VulkanCommandBuilder&)> commands) {
    auto commandBuffer = beginRecording(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VulkanCommandBuilder builder(m_device, commandBuffer);
    commands(builder);

    endRecording(commandBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_device.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device.getGraphicsQueue());

    freeCommandBuffer(commandBuffer);
}

VkCommandBuffer VulkanCommandManager::beginRecording(VkCommandBufferUsageFlags usage) {
    auto commandBuffer = createCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = usage;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

    return commandBuffer;
}

VulkanCommandBuilder VulkanCommandManager::record(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags usage) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = usage;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

    return VulkanCommandBuilder(m_device, commandBuffer);
}

void VulkanCommandManager::endRecording(VkCommandBuffer commandBuffer) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }
}

void VulkanCommandManager::submitCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers,
                                               VkQueue queue,
                                               const std::vector<VkSemaphore>& waitSemaphores,
                                               const std::vector<VkPipelineStageFlags>& waitStages,
                                               const std::vector<VkSemaphore>& signalSemaphores,
                                               VkFence fence) {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();

    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    submitInfo.pCommandBuffers = commandBuffers.data();

    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    if (vkQueueSubmit(queue, 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit command buffer!");
    }
}

void VulkanCommandManager::freeCommandBuffer(VkCommandBuffer commandBuffer) {
    vkFreeCommandBuffers(m_device.getLogicalDevice(), m_commandPool, 1, &commandBuffer);
}

void VulkanCommandManager::freeCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers) {
    vkFreeCommandBuffers(m_device.getLogicalDevice(), m_commandPool,
                        static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
}

void VulkanCommandManager::reset() {
    vkResetCommandPool(m_device.getLogicalDevice(), m_commandPool, 0);
}

VulkanCommandBuilder VulkanCommandManager::createSingleTimeBuilder() {
    auto commandBuffer = beginRecording(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    return VulkanCommandBuilder(m_device, commandBuffer);
}

} // namespace Shoonyakasha
