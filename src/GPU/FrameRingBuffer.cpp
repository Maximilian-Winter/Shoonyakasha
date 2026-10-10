//
// FrameRingBuffer.cpp - Host-written GPU memory that is rewritten every frame
//

#include "GPU/FrameRingBuffer.h"

#include <algorithm>
#include <stdexcept>

namespace Shoonyakasha {

FrameRingBuffer::FrameRingBuffer(VmaAllocator allocator, GpuDeleteQueue& deleteQueue,
                                 VkBufferUsageFlags usage, uint32_t framesInFlight)
    : m_allocator(allocator), m_deleteQueue(deleteQueue), m_usage(usage),
      m_frames(std::max(framesInFlight, 1u)) {}

VkDeviceSize FrameRingBuffer::grownCapacity(VkDeviceSize current, VkDeviceSize needed) {
    if (needed <= current) return current;
    VkDeviceSize capacity = kMinCapacity;
    while (capacity < needed) capacity *= 2;
    return capacity;
}

void FrameRingBuffer::beginFrame(uint32_t frameIndex, VkDeviceSize bytes) {
    m_current = frameIndex % static_cast<uint32_t>(m_frames.size());
    m_offset = 0;

    Frame& frame = m_frames[m_current];
    const VkDeviceSize current = frame.buffer ? frame.buffer->size : 0;
    const VkDeviceSize capacity = grownCapacity(current, bytes);
    if (capacity == current) return;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = capacity;
    bufferInfo.usage = m_usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    GPUBuffer buffer;
    buffer.size = capacity;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, &info) != VK_SUCCESS) {
        throw std::runtime_error("FrameRingBuffer: failed to create a buffer");
    }

    // The old buffer is retired, and freed once no frame in flight reads it.
    frame.buffer = m_deleteQueue.adopt(buffer);
    frame.mapped = info.pMappedData;
}

std::optional<FrameRingBuffer::Slice> FrameRingBuffer::allocate(VkDeviceSize size, VkDeviceSize alignment) {
    const Frame& frame = m_frames[m_current];
    if (!frame.buffer) return std::nullopt;

    const VkDeviceSize offset = alignUp(m_offset, alignment);
    if (offset + size > frame.buffer->size) return std::nullopt;
    m_offset = offset + size;
    return Slice{frame.buffer->buffer, offset, static_cast<uint8_t*>(frame.mapped) + offset};
}

void FrameRingBuffer::flush() {
    const Frame& frame = m_frames[m_current];
    if (frame.buffer && m_offset > 0) {
        vmaFlushAllocation(m_allocator, frame.buffer->allocation, 0, m_offset);
    }
}

VkDeviceSize FrameRingBuffer::capacity(uint32_t frameIndex) const {
    const Frame& frame = m_frames[frameIndex % m_frames.size()];
    return frame.buffer ? frame.buffer->size : 0;
}

} // namespace Shoonyakasha
