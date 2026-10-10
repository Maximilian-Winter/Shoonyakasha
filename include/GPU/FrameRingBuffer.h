//
// FrameRingBuffer.h - Host-written GPU memory that is rewritten every frame
//
// One persistently mapped buffer per frame in flight. A frame's buffer is
// written once that frame's fence has been waited on, so the GPU is no longer
// reading it. beginFrame() makes room for everything the frame will write;
// a buffer that is too small is replaced, and the old one is retired through
// the GpuDeleteQueue.
//

#pragma once

#include "GPU/GpuDeleteQueue.h"
#include "GPU/GPUTypes.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace Shoonyakasha {

class FrameRingBuffer {
public:
    struct Slice {
        VkBuffer     buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        void*        data   = nullptr;  // host address of offset
    };

    static constexpr VkDeviceSize kMinCapacity = 64 * 1024;
    static constexpr VkDeviceSize kDefaultAlignment = 16;

    FrameRingBuffer(VmaAllocator allocator, GpuDeleteQueue& deleteQueue,
                    VkBufferUsageFlags usage, uint32_t framesInFlight);

    FrameRingBuffer(const FrameRingBuffer&) = delete;
    FrameRingBuffer& operator=(const FrameRingBuffer&) = delete;

    /// Starts writing frame `frameIndex`'s buffer, after its fence was waited
    /// on. Grows it to hold at least `bytes`, counting the alignment padding
    /// of every allocate() the frame makes.
    void beginFrame(uint32_t frameIndex, VkDeviceSize bytes);

    /// The next `size` bytes of the current frame's buffer, at an offset that
    /// is a multiple of `alignment`. Nothing if they do not fit.
    std::optional<Slice> allocate(VkDeviceSize size, VkDeviceSize alignment = kDefaultAlignment);

    /// Makes this frame's writes visible to the device. Call after the last
    /// write and before the commands that read them are submitted.
    void flush();

    VkDeviceSize capacity(uint32_t frameIndex) const;

    /// The capacity a buffer of `current` bytes grows to so it holds `needed`
    /// bytes: unchanged when it already fits, else the next power of two of
    /// at least kMinCapacity.
    static VkDeviceSize grownCapacity(VkDeviceSize current, VkDeviceSize needed);

    static VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
        return alignment > 1 ? (value + alignment - 1) / alignment * alignment : value;
    }

private:
    struct Frame {
        GpuBufferRef buffer;
        void*        mapped = nullptr;
    };

    VmaAllocator       m_allocator;
    GpuDeleteQueue&    m_deleteQueue;
    VkBufferUsageFlags m_usage;
    std::vector<Frame> m_frames;
    uint32_t           m_current = 0;
    VkDeviceSize       m_offset = 0;
};

} // namespace Shoonyakasha
