//
// StagingBufferManager.cpp — Ring-buffered CPU↔GPU transfer management
//
// 輪廻之流 — The wheel of data flows between worlds
//

#include "FrameGraph/StagingBufferManager.h"
#include "Vulkan/FrameGraph/FrameGraph.h"  // ReadbackResult, BufferUpdateFrequency, etc.
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/VulkanDevice.h"
#include "Core/Logger.h"

#include <cstring>

namespace Shoonyakasha {
namespace FrameGraph {

StagingBufferManager::StagingBufferManager(VulkanDevice& device, Logger* logger)
    : m_device(device)
    , m_logger(logger)
{
}

StagingBufferManager::~StagingBufferManager() {
    destroy();
}

void StagingBufferManager::create(const std::vector<BufferConfig>& configs, uint32_t maxFramesInFlight) {
    auto* logger = m_logger;

    for (const auto& cfg : configs) {
        StagingEntry entry;
        entry.name = cfg.name;
        entry.gpuBuffer = cfg.gpuBuffer;
        entry.size = cfg.size;
        entry.elementCount = cfg.elementCount;
        entry.elementStride = cfg.elementStride;
        entry.ringDepth = (cfg.ringDepthOverride > 0) ? cfg.ringDepthOverride : maxFramesInFlight;

        entry.hasUpload = cfg.hasUpload;
        entry.uploadFrequency = cfg.uploadFrequency;
        entry.uploadN = cfg.uploadN;

        entry.hasReadback = cfg.hasReadback;
        entry.readbackFrequency = cfg.readbackFrequency;
        entry.readbackN = cfg.readbackN;

        // Create upload staging ring (CPU→GPU)
        if (entry.hasUpload) {
            entry.uploadStaging.resize(entry.ringDepth);

            for (uint32_t i = 0; i < entry.ringDepth; ++i) {
                entry.uploadStaging[i] = std::make_unique<VulkanBuffer>(
                    m_device,
                    entry.size,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
            }

            if (logger) {
                logger->log(LogLevel::Info,
                    "Created staging upload ring for '%s' (%u buffers x %zu bytes)",
                    entry.name.c_str(), entry.ringDepth, static_cast<size_t>(entry.size));
            }
        }

        // Create readback staging ring (GPU→CPU)
        if (entry.hasReadback) {
            entry.readbackStaging.resize(entry.ringDepth);
            entry.readbackCpuBuffers.resize(entry.ringDepth);
            entry.readbackPendingFrame.resize(entry.ringDepth, 0);

            for (uint32_t i = 0; i < entry.ringDepth; ++i) {
                entry.readbackStaging[i] = std::make_unique<VulkanBuffer>(
                    m_device,
                    entry.size,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
                // Pre-allocate CPU-side buffer for this slot
                entry.readbackCpuBuffers[i].resize(static_cast<size_t>(entry.size));
            }

            if (logger) {
                float sizeMB = static_cast<float>(entry.size * entry.ringDepth) / (1024.0f * 1024.0f);
                logger->log(LogLevel::Info,
                    "Created staging readback ring for '%s' (%u buffers x %.2f MB)",
                    entry.name.c_str(), entry.ringDepth, sizeMB);
            }
        }

        m_entries[cfg.name] = std::move(entry);
    }
}

void StagingBufferManager::destroy() {
    for (auto& [name, entry] : m_entries) {
        for (auto& buf : entry.uploadStaging) {
            buf.reset();
        }
        for (auto& buf : entry.readbackStaging) {
            buf.reset();
        }
        entry.readbackCpuBuffers.clear();
    }
    m_entries.clear();

    // Clean up image staging entries
    for (auto& [name, entry] : m_imageEntries) {
        for (auto& buf : entry.readbackStaging) {
            buf.reset();
        }
        entry.readbackCpuBuffers.clear();
    }
    m_imageEntries.clear();

    m_pendingResults.clear();
}

void StagingBufferManager::registerReadbackCallback(const std::string& name, ReadbackCallbackFn cb) {
    auto it = m_entries.find(name);
    if (it != m_entries.end()) {
        it->second.callback = std::move(cb);
    }
}

void StagingBufferManager::triggerReadback(const std::string& name) {
    auto it = m_entries.find(name);
    if (it != m_entries.end() && it->second.hasReadback) {
        it->second.readbackTriggered = true;
    }
}

void StagingBufferManager::writeUploadData(const std::string& name, uint32_t frameIndex,
                                            const void* data, VkDeviceSize size) {
    auto it = m_entries.find(name);
    if (it == m_entries.end() || !it->second.hasUpload) return;

    auto& entry = it->second;
    uint32_t slot = frameIndex % entry.ringDepth;

    // Use VulkanBuffer::update() to write data into the staging buffer
    // update() handles map/memcpy/unmap internally
    VkDeviceSize copySize = (size < entry.size) ? size : entry.size;
    entry.uploadStaging[slot]->update(data, copySize, 0);
    entry.uploadDirty = true;
}

void StagingBufferManager::markUploadDirty(const std::string& name) {
    auto it = m_entries.find(name);
    if (it != m_entries.end()) {
        it->second.uploadDirty = true;
    }
}

bool StagingBufferManager::shouldReadback(const StagingEntry& entry, uint64_t frameNumber) const {
    if (entry.readbackTriggered) return true;  // Phase 4: One-shot override
    switch (entry.readbackFrequency) {
        case BufferUpdateFrequency::PerFrame:
            return true;
        case BufferUpdateFrequency::EveryNFrames:
            return (entry.readbackN > 0) && (frameNumber % entry.readbackN == 0);
        case BufferUpdateFrequency::Once:
            return frameNumber == 0;
        case BufferUpdateFrequency::Manual:
        default:
            return false;
    }
}

bool StagingBufferManager::shouldUpload(const StagingEntry& entry, uint64_t frameNumber) const {
    if (!entry.uploadDirty) return false;

    switch (entry.uploadFrequency) {
        case BufferUpdateFrequency::PerFrame:
            return true;
        case BufferUpdateFrequency::EveryNFrames:
            return (entry.uploadN > 0) && (frameNumber % entry.uploadN == 0);
        case BufferUpdateFrequency::Once:
            return frameNumber == 0;
        case BufferUpdateFrequency::OnChange:
            return true;  // Dirty flag already checked above
        case BufferUpdateFrequency::Manual:
        default:
            return false;
    }
}

void StagingBufferManager::recordUploadCommands(VkCommandBuffer cmd, uint32_t frameIndex, uint64_t frameNumber) {
    for (auto& [name, entry] : m_entries) {
        if (!entry.hasUpload || !shouldUpload(entry, frameNumber)) continue;

        uint32_t slot = frameIndex % entry.ringDepth;
        VkBuffer stagingBuf = entry.uploadStaging[slot]->getBuffer();

        // Barrier: ensure host writes to staging buffer are visible to transfer engine
        VkBufferMemoryBarrier preCopyBarrier{};
        preCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        preCopyBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        preCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        preCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.buffer = stagingBuf;
        preCopyBarrier.offset = 0;
        preCopyBarrier.size = entry.size;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr,
            1, &preCopyBarrier,
            0, nullptr);

        // Copy staging → GPU buffer
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = entry.size;
        vkCmdCopyBuffer(cmd, stagingBuf, entry.gpuBuffer, 1, &copyRegion);

        // Barrier: make GPU buffer visible to shader reads/writes
        VkBufferMemoryBarrier postCopyBarrier{};
        postCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        postCopyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        postCopyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        postCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarrier.buffer = entry.gpuBuffer;
        postCopyBarrier.offset = 0;
        postCopyBarrier.size = entry.size;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0, 0, nullptr,
            1, &postCopyBarrier,
            0, nullptr);

        entry.uploadDirty = false;
    }
}

void StagingBufferManager::recordReadbackCommands(VkCommandBuffer cmd, uint32_t frameIndex, uint64_t frameNumber) {
    for (auto& [name, entry] : m_entries) {
        if (!entry.hasReadback || !shouldReadback(entry, frameNumber)) continue;

        uint32_t slot = frameIndex % entry.ringDepth;

        // Barrier: ensure shader writes are complete before transfer read
        VkBufferMemoryBarrier preCopyBarrier{};
        preCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        preCopyBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        preCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        preCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.buffer = entry.gpuBuffer;
        preCopyBarrier.offset = 0;
        preCopyBarrier.size = entry.size;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr,
            1, &preCopyBarrier,
            0, nullptr);

        // Copy GPU buffer → readback staging
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = entry.size;
        vkCmdCopyBuffer(cmd, entry.gpuBuffer, entry.readbackStaging[slot]->getBuffer(), 1, &copyRegion);

        // Mark this slot as having a pending readback
        // +1 so 0 means "no pending"
        entry.readbackPendingFrame[slot] = frameNumber + 1;

        // Phase 4: Clear one-shot trigger after recording
        entry.readbackTriggered = false;
    }
}

void StagingBufferManager::processCompletedReadbacks(uint32_t frameIndex, uint64_t frameNumber) {
    // When this function is called, the app has already waited on the fence for frameIndex.
    // Any copy command recorded into that frame's command buffer is now complete.
    // The readback staging buffer at slot (frameIndex % ringDepth) is safe to read.

    for (auto& [name, entry] : m_entries) {
        if (!entry.hasReadback) continue;

        uint32_t slot = frameIndex % entry.ringDepth;

        // Check if this slot has a pending readback
        if (entry.readbackPendingFrame[slot] == 0) continue;

        // Read data from the staging buffer into CPU-side storage
        // map → copyTo → unmap  (staging is host-visible + coherent, so this is fast)
        entry.readbackStaging[slot]->map();
        entry.readbackStaging[slot]->copyTo(
            entry.readbackCpuBuffers[slot].data(),
            entry.size
        );
        entry.readbackStaging[slot]->unmap();

        // Build result
        ReadbackResult result;
        result.bufferName = entry.name;
        result.data = entry.readbackCpuBuffers[slot].data();
        result.size = entry.size;
        result.elementCount = entry.elementCount;
        result.elementStride = entry.elementStride;
        result.frameNumber = entry.readbackPendingFrame[slot] - 1;  // Undo the +1

        // Invoke callback if registered
        if (entry.callback) {
            entry.callback(result);
        }

        // Store for polling
        m_pendingResults.push_back(result);

        // Clear pending flag
        entry.readbackPendingFrame[slot] = 0;
    }
}

std::vector<ReadbackResult> StagingBufferManager::pollReadbacks() {
    std::vector<ReadbackResult> results = std::move(m_pendingResults);
    m_pendingResults.clear();
    return results;
}

// ═══════════════════════════════════════════════════════════════
// Image Readback — render target data flow
// 映像之流 — The flow of images between worlds
// ═══════════════════════════════════════════════════════════════

void StagingBufferManager::createImages(const std::vector<ImageConfig>& configs, uint32_t maxFramesInFlight) {
    for (const auto& cfg : configs) {
        ImageStagingEntry entry;
        entry.name = cfg.name;
        entry.gpuImage = cfg.gpuImage;
        entry.format = cfg.format;
        entry.extent = cfg.extent;
        entry.bufferSize = cfg.bufferSize;
        entry.aspectMask = cfg.aspectMask;
        entry.ringDepth = (cfg.ringDepthOverride > 0) ? cfg.ringDepthOverride : maxFramesInFlight;

        entry.hasReadback = cfg.hasReadback;
        entry.readbackFrequency = cfg.readbackFrequency;
        entry.readbackN = cfg.readbackN;

        if (entry.hasReadback) {
            entry.readbackStaging.resize(entry.ringDepth);
            entry.readbackCpuBuffers.resize(entry.ringDepth);
            entry.readbackPendingFrame.resize(entry.ringDepth, 0);

            for (uint32_t i = 0; i < entry.ringDepth; ++i) {
                entry.readbackStaging[i] = std::make_unique<VulkanBuffer>(
                    m_device,
                    entry.bufferSize,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
                entry.readbackCpuBuffers[i].resize(static_cast<size_t>(entry.bufferSize));
            }

            if (m_logger) {
                float sizeMB = static_cast<float>(entry.bufferSize * entry.ringDepth) / (1024.0f * 1024.0f);
                m_logger->log(LogLevel::Info,
                    "Created image readback staging for '%s' (%ux%u, %u buffers x %.2f MB)",
                    entry.name.c_str(), entry.extent.width, entry.extent.height,
                    entry.ringDepth, sizeMB);
            }
        }

        m_imageEntries[cfg.name] = std::move(entry);
    }
}

void StagingBufferManager::updateImageLayout(const std::string& name, VkImageLayout layout) {
    auto it = m_imageEntries.find(name);
    if (it != m_imageEntries.end()) {
        it->second.currentLayout = layout;
    }
}

bool StagingBufferManager::shouldImageReadback(const ImageStagingEntry& entry, uint64_t frameNumber) const {
    if (entry.readbackTriggered) return true;
    switch (entry.readbackFrequency) {
        case BufferUpdateFrequency::PerFrame:      return true;
        case BufferUpdateFrequency::EveryNFrames:  return (entry.readbackN > 0) && (frameNumber % entry.readbackN == 0);
        case BufferUpdateFrequency::Once:          return frameNumber == 0;
        case BufferUpdateFrequency::Manual:
        default:                                   return false;
    }
}

void StagingBufferManager::recordImageReadbackCommands(VkCommandBuffer cmd, uint32_t frameIndex, uint64_t frameNumber) {
    for (auto& [name, entry] : m_imageEntries) {
        if (!entry.hasReadback || !shouldImageReadback(entry, frameNumber)) continue;

        uint32_t slot = frameIndex % entry.ringDepth;

        // Barrier: transition image to TRANSFER_SRC_OPTIMAL
        VkImageMemoryBarrier preCopyBarrier{};
        preCopyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        preCopyBarrier.oldLayout = entry.currentLayout;
        preCopyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        preCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preCopyBarrier.image = entry.gpuImage;
        preCopyBarrier.subresourceRange.aspectMask = entry.aspectMask;
        preCopyBarrier.subresourceRange.baseMipLevel = 0;
        preCopyBarrier.subresourceRange.levelCount = 1;
        preCopyBarrier.subresourceRange.baseArrayLayer = 0;
        preCopyBarrier.subresourceRange.layerCount = 1;
        preCopyBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        preCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &preCopyBarrier);

        // Copy image to staging buffer
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = entry.aspectMask;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {entry.extent.width, entry.extent.height, 1};

        vkCmdCopyImageToBuffer(cmd, entry.gpuImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                entry.readbackStaging[slot]->getBuffer(), 1, &region);

        // Barrier: transition image back to original layout
        VkImageMemoryBarrier postCopyBarrier{};
        postCopyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        postCopyBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        postCopyBarrier.newLayout = entry.currentLayout;
        postCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postCopyBarrier.image = entry.gpuImage;
        postCopyBarrier.subresourceRange.aspectMask = entry.aspectMask;
        postCopyBarrier.subresourceRange.baseMipLevel = 0;
        postCopyBarrier.subresourceRange.levelCount = 1;
        postCopyBarrier.subresourceRange.baseArrayLayer = 0;
        postCopyBarrier.subresourceRange.layerCount = 1;
        postCopyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        postCopyBarrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &postCopyBarrier);

        entry.readbackPendingFrame[slot] = frameNumber + 1;
        entry.readbackTriggered = false;
    }
}

void StagingBufferManager::processCompletedImageReadbacks(uint32_t frameIndex, uint64_t frameNumber) {
    for (auto& [name, entry] : m_imageEntries) {
        if (!entry.hasReadback) continue;

        uint32_t slot = frameIndex % entry.ringDepth;
        if (entry.readbackPendingFrame[slot] == 0) continue;

        // Read from staging buffer
        entry.readbackStaging[slot]->map();
        entry.readbackStaging[slot]->copyTo(
            entry.readbackCpuBuffers[slot].data(),
            entry.bufferSize
        );
        entry.readbackStaging[slot]->unmap();

        // Build result with image-specific metadata
        ReadbackResult result;
        result.bufferName = entry.name;
        result.data = entry.readbackCpuBuffers[slot].data();
        result.size = entry.bufferSize;
        result.elementCount = entry.extent.width * entry.extent.height;
        result.elementStride = static_cast<uint32_t>(entry.bufferSize / result.elementCount);
        result.frameNumber = entry.readbackPendingFrame[slot] - 1;
        result.isImage = true;
        result.imageFormat = entry.format;
        result.imageWidth = entry.extent.width;
        result.imageHeight = entry.extent.height;

        if (entry.callback) {
            entry.callback(result);
        }

        m_pendingResults.push_back(result);
        entry.readbackPendingFrame[slot] = 0;
    }
}

void StagingBufferManager::registerImageReadbackCallback(const std::string& name, ReadbackCallbackFn cb) {
    auto it = m_imageEntries.find(name);
    if (it != m_imageEntries.end()) {
        it->second.callback = std::move(cb);
    }
}

void StagingBufferManager::triggerImageReadback(const std::string& name) {
    auto it = m_imageEntries.find(name);
    if (it != m_imageEntries.end() && it->second.hasReadback) {
        it->second.readbackTriggered = true;
    }
}

} // namespace FrameGraph
} // namespace Shoonyakasha
