//
// PixelReadback.cpp - One texel of a graph image copied back to the CPU
//

#include "Vulkan/FrameGraph/PixelReadback.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

#include <glm/gtc/packing.hpp>

#include <cstring>

namespace Shoonyakasha {
namespace FrameGraph {

uint32_t pixelReadbackTexelSize(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_R32_SFLOAT:
            return 4;
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_R16_SFLOAT:
            return 2;
        case VK_FORMAT_R8_UNORM:
            return 1;
        default:
            return 0;
    }
}

VkImageAspectFlags pixelReadbackAspect(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D16_UNORM:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

float decodeReadbackTexel(VkFormat format, const void* texel) {
    switch (format) {
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_R32_SFLOAT: {
            float value;
            std::memcpy(&value, texel, sizeof(value));
            return value;
        }
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D24_UNORM_S8_UINT: {
            // The depth aspect of a 24-bit format copies as 32 bits with the
            // depth in the low 24.
            uint32_t bits;
            std::memcpy(&bits, texel, sizeof(bits));
            return static_cast<float>(bits & 0x00FFFFFFu) / 16777215.0f;
        }
        case VK_FORMAT_D16_UNORM: {
            uint16_t bits;
            std::memcpy(&bits, texel, sizeof(bits));
            return static_cast<float>(bits) / 65535.0f;
        }
        case VK_FORMAT_R16_SFLOAT: {
            uint16_t bits;
            std::memcpy(&bits, texel, sizeof(bits));
            return glm::unpackHalf1x16(bits);
        }
        case VK_FORMAT_R8_UNORM:
            return static_cast<float>(*static_cast<const uint8_t*>(texel)) / 255.0f;
        default:
            return 0.0f;
    }
}

std::optional<VkImageLayout> layoutAfterPass(const CompiledPass& pass, ResourceHandle resource) {
    for (const auto& barrier : pass.postBarriers) {
        if (barrier.resource == resource) return barrier.newLayout;
    }
    if (pass.hasDepthAttachment && pass.depthAttachment.resource == resource) return pass.depthAttachment.layout;
    for (const auto& attachment : pass.colorAttachments) {
        if (attachment.resource == resource) return attachment.layout;
    }
    return std::nullopt;
}

std::array<VkImageMemoryBarrier2, 2> pixelReadbackBarriers(VkImage image, VkImageLayout layout,
                                                           VkImageAspectFlags aspect) {
    VkImageMemoryBarrier2 toCopy{};
    toCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toCopy.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    toCopy.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    toCopy.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toCopy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    toCopy.oldLayout = layout;
    toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy.image = image;
    toCopy.subresourceRange = {aspect, 0, 1, 0, 1};

    // The copy only reads, so the way back needs no access made available;
    // it must still finish before later passes touch the image.
    VkImageMemoryBarrier2 back = toCopy;
    back.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    back.srcAccessMask = VK_ACCESS_2_NONE;
    back.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    back.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = layout;
    return {toCopy, back};
}

void PixelReadbackQueue::setFramesInFlight(uint32_t count) {
    m_slots.assign(count, Slot{});
}

void PixelReadbackQueue::recorded(uint32_t frameIndex, VkFormat format, uint64_t frame) {
    if (frameIndex >= m_slots.size()) m_slots.resize(frameIndex + 1);
    Slot& slot = m_slots[frameIndex];
    slot.awaiting = true;
    slot.format = format;
    slot.pixel = m_next ? m_next->pixel : VkOffset2D{0, 0};
    slot.frame = frame;
    m_next.reset();
}

bool PixelReadbackQueue::awaiting(uint32_t frameIndex) const {
    return frameIndex < m_slots.size() && m_slots[frameIndex].awaiting;
}

void PixelReadbackQueue::complete(uint32_t frameIndex, const void* texel) {
    if (!awaiting(frameIndex)) return;
    Slot& slot = m_slots[frameIndex];
    slot.awaiting = false;
    // Frames in flight finish in order, but keep the newest if they do not.
    if (m_latest.valid && slot.frame < m_latest.frame) return;
    m_latest.valid = true;
    m_latest.value = decodeReadbackTexel(slot.format, texel);
    m_latest.pixel = slot.pixel;
    m_latest.frame = slot.frame;
}

} // namespace FrameGraph
} // namespace Shoonyakasha
