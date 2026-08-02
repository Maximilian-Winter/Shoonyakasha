//
// GpuDeleteQueue.cpp
//

#include "GPU/GpuDeleteQueue.h"
#include "GPU/GPUResourceFactory.h"

namespace Shoonyakasha {

GpuDeleteQueue::GpuDeleteQueue(VmaAllocator allocator, uint32_t framesInFlight)
    : m_allocator(allocator)
    , m_framesInFlight(framesInFlight == 0 ? 1 : framesInFlight)
{
}

GpuDeleteQueue::~GpuDeleteQueue() {
    // Free everything still held. A buffer still referenced at this point
    // outlives the queue, which is a declaration-order error at the owner.
    flush();
}

GpuBufferRef GpuDeleteQueue::adopt(const GPUBuffer& buffer) {
    if (!buffer.isValid()) {
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_live;
    }

    // The allocation is owned by the deleter, not by the shared_ptr's storage:
    // GPUBuffer is a handle struct with no destructor.
    GpuDeleteQueue* queue = this;
    return GpuBufferRef(new GPUBuffer(buffer), [queue](const GPUBuffer* b) {
        queue->retire(*b);
        delete b;
    });
}

void GpuDeleteQueue::retire(const GPUBuffer& buffer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_live > 0) {
        --m_live;
    }
    m_pending.push_back(Pending{buffer, m_frame});
}

void GpuDeleteQueue::beginFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_frame;
    collectLocked(false);
}

void GpuDeleteQueue::setFramesInFlight(uint32_t framesInFlight) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_framesInFlight = (framesInFlight == 0 ? 1 : framesInFlight);
}

void GpuDeleteQueue::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    collectLocked(true);
}

void GpuDeleteQueue::collectLocked(bool force) {
    size_t keep = 0;
    for (size_t i = 0; i < m_pending.size(); ++i) {
        const bool safe = force || (m_pending[i].retiredFrame + m_framesInFlight <= m_frame);
        if (safe) {
            GPUBuffer buffer = m_pending[i].buffer;
            GPUResourceFactory::destroyBuffer(m_allocator, buffer);
        } else {
            m_pending[keep++] = m_pending[i];
        }
    }
    m_pending.resize(keep);
}

size_t GpuDeleteQueue::pendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending.size();
}

size_t GpuDeleteQueue::liveCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_live;
}

} // namespace Shoonyakasha
