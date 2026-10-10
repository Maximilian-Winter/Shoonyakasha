//
// UIRenderer.cpp - Drawing canvas UI batches with Vulkan
//

#include "UI/UIRenderer.h"

#include "Core/Logger.h"
#include "GPU/FrameRingBuffer.h"
#include "GPU/GPUResourceFactory.h"
#include "UI/GlyphCache.h"
#include "Vulkan/FrameGraph/FrameGraphPass.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanPipeline.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Shoonyakasha {
namespace UI {

namespace {

constexpr uint32_t kSetsPerPool = 64;

VkDescriptorPool createPool(VkDevice device) {
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerPool * 2};
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.maxSets = kSetsPerPool;
    info.poolSizeCount = 1;
    info.pPoolSizes = &size;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &info, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("UIRenderer: failed to create a descriptor pool");
    }
    return pool;
}

VkImageMemoryBarrier2 atlasBarrier(VkImage image, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Earlier frames may still sample the atlas.
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    } else {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }
    return barrier;
}

void recordBarrier(VkCommandBuffer cmd, const VkImageMemoryBarrier2& barrier) {
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

UIRenderer::UIRenderer(VulkanDevice& device, std::string shaderDirectory, uint32_t framesInFlight,
                       int atlasSize, Logger* logger)
    : m_device(device), m_shaderDirectory(std::move(shaderDirectory)), m_logger(logger),
      m_descriptors(std::max(framesInFlight, 1u)) {
    const VkDevice vkDevice = device.getLogicalDevice();
    const VmaAllocator allocator = device.getAllocator().getHandle();

    m_atlas = GPUResourceFactory::createTexture2D(allocator, vkDevice, static_cast<uint32_t>(atlasSize),
                                                  static_cast<uint32_t>(atlasSize), VK_FORMAT_R8_UNORM);
    m_atlas.sampler = GPUResourceFactory::createSampler(vkDevice, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                                        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f,
                                                        VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f);

    m_ring = std::make_unique<FrameRingBuffer>(
        allocator, device.getDeleteQueue(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        static_cast<uint32_t>(m_descriptors.size()));

    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i;  // 0: glyph atlas, 1: the batch image
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        throw std::runtime_error("UIRenderer: failed to create the descriptor set layout");
    }
}

UIRenderer::~UIRenderer() {
    const VkDevice vkDevice = m_device.getLogicalDevice();
    m_pipelines.clear();
    for (auto& frame : m_descriptors) {
        for (VkDescriptorPool pool : frame.pools) vkDestroyDescriptorPool(vkDevice, pool, nullptr);
    }
    if (m_setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vkDevice, m_setLayout, nullptr);
    GPUResourceFactory::destroyTexture(m_device.getAllocator().getHandle(), vkDevice, m_atlas);
}

void UIRenderer::prepareFrame(VkCommandBuffer cmd, uint32_t frameIndex, const UIDrawData& drawData,
                              GlyphCache& glyphs) {
    m_frame = frameIndex % static_cast<uint32_t>(m_descriptors.size());

    auto& descriptors = m_descriptors[m_frame];
    for (VkDescriptorPool pool : descriptors.pools) vkResetDescriptorPool(m_device.getLogicalDevice(), pool, 0);
    descriptors.sets.clear();

    const VkDeviceSize vertexBytes = drawData.vertices.size() * sizeof(UIVertex);
    const VkDeviceSize indexBytes = drawData.indices.size() * sizeof(uint32_t);
    VkDeviceSize bytes = FrameRingBuffer::alignUp(vertexBytes, FrameRingBuffer::kDefaultAlignment) +
                         FrameRingBuffer::alignUp(indexBytes, FrameRingBuffer::kDefaultAlignment);
    for (const auto& upload : glyphs.pendingUploads()) {
        bytes += FrameRingBuffer::alignUp(upload.bitmap.pixels.size(), FrameRingBuffer::kDefaultAlignment);
    }
    m_ring->beginFrame(m_frame, bytes);

    uploadGlyphs(cmd, glyphs);

    m_indexCount = 0;
    if (!drawData.indices.empty()) {
        const auto vertices = m_ring->allocate(vertexBytes);
        const auto indices = m_ring->allocate(indexBytes);
        if (vertices && indices) {
            std::memcpy(vertices->data, drawData.vertices.data(), vertexBytes);
            std::memcpy(indices->data, drawData.indices.data(), indexBytes);
            m_vertexBuffer = vertices->buffer;
            m_vertexOffset = vertices->offset;
            m_indexBuffer = indices->buffer;
            m_indexOffset = indices->offset;
            m_indexCount = drawData.indices.size();
        }
    }
    m_ring->flush();
}

void UIRenderer::uploadGlyphs(VkCommandBuffer cmd, GlyphCache& glyphs) {
    const auto& uploads = glyphs.pendingUploads();
    if (m_atlasReady && uploads.empty()) return;

    recordBarrier(cmd, atlasBarrier(m_atlas.image,
                                    m_atlasReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));

    if (!m_atlasReady) {
        // Filtering at a glyph's edge reads its neighbours, which must be
        // empty rather than whatever the memory held.
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, m_atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    }

    for (const auto& upload : uploads) {
        const auto slice = m_ring->allocate(upload.bitmap.pixels.size());
        if (!slice) break;
        std::memcpy(slice->data, upload.bitmap.pixels.data(), upload.bitmap.pixels.size());

        VkBufferImageCopy region{};
        region.bufferOffset = slice->offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {upload.position.x, upload.position.y, 0};
        region.imageExtent = {static_cast<uint32_t>(upload.bitmap.size.x),
                              static_cast<uint32_t>(upload.bitmap.size.y), 1};
        vkCmdCopyBufferToImage(cmd, slice->buffer, m_atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    glyphs.clearPendingUploads();

    recordBarrier(cmd, atlasBarrier(m_atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    m_atlasReady = true;
}

VulkanPipeline* UIRenderer::pipelineFor(VkFormat color, VkFormat depth) {
    const PipelineKey key{color, depth};
    if (auto it = m_pipelines.find(key); it != m_pipelines.end()) return it->second.get();

    std::unique_ptr<VulkanPipeline> pipeline;
    try {
        const std::vector<VkVertexInputBindingDescription> bindings = {
            {0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
        const std::vector<VkVertexInputAttributeDescription> attributes = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(UIVertex, position))},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(UIVertex, uv))},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(offsetof(UIVertex, color))},
            {3, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(offsetof(UIVertex, mode))},
        };

        RenderingFormats formats;
        formats.color = {color};
        formats.depth = depth;

        pipeline = PipelineStateBuilder()
            .withShaders(m_shaderDirectory + "ui.vert.spv", m_shaderDirectory + "ui.frag.spv")
            .withCustomVertexInput(bindings, attributes)
            .withCulling(VK_CULL_MODE_NONE)
            .withDepthTest(false)
            .withDepthWrite(false)
            .withCustomBlending(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
                                VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD)
            .withDynamicViewport()
            .withDynamicScissor()
            .withDescriptorSetLayout(m_setLayout)
            .withPushConstants(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::vec2))
            .buildPipeline(m_device, formats, VkExtent2D{1, 1});
    } catch (const std::exception& e) {
        if (m_logger) {
            m_logger->log(LogLevel::Error, "Canvas UI: no pipeline for colour format %d: %s",
                          static_cast<int>(color), e.what());
        }
    }
    return m_pipelines.emplace(key, std::move(pipeline)).first->second.get();
}

VkDescriptorSet UIRenderer::descriptorSetFor(VkImageView view, VkSampler sampler) {
    auto& frame = m_descriptors[m_frame];
    if (auto it = frame.sets.find(view); it != frame.sets.end()) return it->second;

    const VkDevice vkDevice = m_device.getLogicalDevice();
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorSetCount = 1;
    info.pSetLayouts = &m_setLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    for (VkDescriptorPool pool : frame.pools) {
        info.descriptorPool = pool;
        if (vkAllocateDescriptorSets(vkDevice, &info, &set) == VK_SUCCESS) break;
        set = VK_NULL_HANDLE;
    }
    if (set == VK_NULL_HANDLE) {
        frame.pools.push_back(createPool(vkDevice));
        info.descriptorPool = frame.pools.back();
        if (vkAllocateDescriptorSets(vkDevice, &info, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
    }

    const VkDescriptorImageInfo images[2] = {
        {m_atlas.sampler, m_atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 2;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = images;
    vkUpdateDescriptorSets(vkDevice, 1, &write, 0, nullptr);

    frame.sets.emplace(view, set);
    return set;
}

void UIRenderer::drawOverlays(const FrameGraph::PassExecuteContext& ctx, const UIDrawData& drawData) {
    if (m_indexCount == 0 || !m_atlasReady) return;
    if (ctx.colorAttachmentCount != 1) {
        if (m_logger) {
            m_logger->logEvery(5.0f, LogLevel::Warning,
                               "Canvas UI: a ui_canvas pass needs exactly one colour output, it has %u",
                               ctx.colorAttachmentCount);
        }
        return;
    }

    VulkanPipeline* pipeline = pipelineFor(ctx.colorFormat, ctx.depthFormat);
    if (!pipeline) return;

    const VkCommandBuffer cmd = ctx.cmd.getHandle();
    const VkExtent2D extent = ctx.renderExtent;
    pipeline->bind(cmd);

    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                              0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &m_vertexOffset);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, m_indexOffset, VK_INDEX_TYPE_UINT32);

    const VkPipelineLayout layout = pipeline->getLayout();
    for (const auto& canvas : drawData.canvases) {
        if (canvas.mode != UICanvas::Mode::ScreenOverlay || canvas.batchCount == 0) continue;
        if (canvas.targetSize.x <= 0.0f || canvas.targetSize.y <= 0.0f) continue;
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2), &canvas.targetSize);

        for (uint32_t b = canvas.firstBatch; b < canvas.firstBatch + canvas.batchCount; ++b) {
            const UIBatch& batch = drawData.batches[b];
            const int32_t x0 = std::clamp(batch.scissor.x, 0, static_cast<int32_t>(extent.width));
            const int32_t y0 = std::clamp(batch.scissor.y, 0, static_cast<int32_t>(extent.height));
            const int32_t x1 = std::clamp(batch.scissor.x + batch.scissor.z, x0, static_cast<int32_t>(extent.width));
            const int32_t y1 = std::clamp(batch.scissor.y + batch.scissor.w, y0, static_cast<int32_t>(extent.height));
            if (x1 <= x0 || y1 <= y0 || batch.indexCount == 0) continue;

            // A batch without Image quads still binds an image; the atlas
            // stands in. So does the atlas sampler for a texture without one.
            const VkDescriptorSet set = batch.image != VK_NULL_HANDLE
                ? descriptorSetFor(batch.image, batch.sampler != VK_NULL_HANDLE ? batch.sampler : m_atlas.sampler)
                : descriptorSetFor(m_atlas.view, m_atlas.sampler);
            if (set == VK_NULL_HANDLE) continue;

            const VkRect2D scissor{{x0, y0}, {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
            vkCmdDrawIndexed(cmd, batch.indexCount, 1, batch.firstIndex, 0, 0);
        }
    }
}

} // namespace UI
} // namespace Shoonyakasha
