//
// GpuDeleteQueue.h - Shared ownership and deferred release for GPU buffers
//
// A buffer is freed once the last reference to it is dropped and the frames
// that may still reference it have completed.
//

#pragma once

#include "GPU/GPUTypes.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Shoonyakasha {

class GpuDeleteQueue;

/// Shared, reference-counted ownership of a GPU buffer.
///
/// Copying shares the same allocation between entities. The buffer is retired
/// when the last reference is dropped. A null ref means no buffer, which is a
/// valid state for an optional index buffer.
using GpuBufferRef = std::shared_ptr<const GPUBuffer>;

/// Wrap a buffer without transferring ownership: nothing is freed when the last
/// reference is dropped. For buffers whose lifetime is managed elsewhere, and
/// for tests with no device.
inline GpuBufferRef borrowBuffer(const GPUBuffer& buffer) {
    return std::make_shared<const GPUBuffer>(buffer);
}

/// Owns GPU buffers that are no longer referenced, and frees them once no
/// in-flight frame can still be reading them.
///
/// Dropping the last `GpuBufferRef` does not call `vmaDestroyBuffer` directly,
/// because a command buffer recorded in an earlier frame may still reference the
/// buffer. The allocation is retired with the current frame index and freed once
/// `framesInFlight` further frames have begun.
///
/// The queue must outlive every `GpuBufferRef` it hands out: the deleter holds a
/// raw pointer to it. `VulkanDevice` owns the queue and declares it after the
/// allocator, so the queue is destroyed first and can still free what it holds.
/// In `ApplicationBase` the scene, and so every `MeshComponent`, is destroyed
/// before the device.
class GpuDeleteQueue {
public:
    /// framesInFlight is the number of frames that may be recorded before the
    /// first is known to have completed. Retired buffers survive that many
    /// beginFrame() calls.
    GpuDeleteQueue(VmaAllocator allocator, uint32_t framesInFlight);
    ~GpuDeleteQueue();

    GpuDeleteQueue(const GpuDeleteQueue&) = delete;
    GpuDeleteQueue& operator=(const GpuDeleteQueue&) = delete;

    /// Take ownership of a buffer and return a shared reference to it. Returns
    /// a null ref for an invalid buffer, so an absent index buffer needs no
    /// special case at the call site.
    GpuBufferRef adopt(const GPUBuffer& buffer);

    /// Advance one frame and free anything old enough. Call once per frame,
    /// before recording.
    void beginFrame();

    /// Set the retention window once the frames-in-flight count is known. Call
    /// before rendering starts: lowering it while frames are in flight can free
    /// a buffer that a recorded command buffer still references.
    void setFramesInFlight(uint32_t framesInFlight);

    /// Free everything pending immediately, ignoring frame age.
    ///
    /// The caller is responsible for the GPU being idle — normally
    /// `vkDeviceWaitIdle`. Intended for shutdown and for level transitions where
    /// waiting is acceptable and reclaiming the memory now matters more.
    void flush();

    /// Buffers retired but not yet freed.
    size_t pendingCount() const;

    /// Buffers adopted and still referenced by someone.
    size_t liveCount() const;

private:
    void retire(const GPUBuffer& buffer);
    void collectLocked(bool force);

    struct Pending {
        GPUBuffer buffer;
        uint64_t  retiredFrame;
    };

    VmaAllocator m_allocator;
    uint32_t     m_framesInFlight;
    uint64_t     m_frame = 0;

    mutable std::mutex   m_mutex;
    std::vector<Pending> m_pending;
    size_t               m_live = 0;
};

} // namespace Shoonyakasha
