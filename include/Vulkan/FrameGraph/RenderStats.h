//
// Shoonyakasha Engine - Render Statistics
//
// Frame rate, frame time, draw and dispatch counts, and per-pass CPU and GPU
// times, collected while the render graph records. Off by default: turned on
// with RenderGraph::setStatsEnabled, the facade's setRenderStatsEnabled, or
// SHOONYAKASHA_STATS=1, which also prints a summary every second.
//
// GPU times come from timestamp queries, each frame in flight with its own
// range, read back when the frame's fence has been waited on, so collecting
// them never stalls. They therefore describe a frame that finished one or two
// frames ago.
//

#pragma once

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Shoonyakasha {
class VulkanDevice;
}

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Draw counting
// ═══════════════════════════════════════════════════════════════

/// Draws and dispatches recorded by one pass, or a whole frame.
struct DrawCounts {
    uint32_t drawCalls  = 0;
    uint32_t dispatches = 0;
    /// Vertex (or, for indexed draws, index) count times instance count,
    /// summed over the draws.
    uint64_t vertices   = 0;

    DrawCounts& operator+=(const DrawCounts& o) {
        drawCalls += o.drawCalls;
        dispatches += o.dispatches;
        vertices += o.vertices;
        return *this;
    }
};

namespace RenderStatsCounting {
/// The counts of the pass being recorded, while render stats are on; null
/// otherwise. Set by FrameGraphExecutor around each pass.
inline thread_local DrawCounts* active = nullptr;
}

/// Count a draw into the pass being recorded. The engine's own draw sites
/// call this; a pass callback that records vkCmdDraw* itself can too, or use
/// the VulkanCommandBuilder's draw methods, which count.
inline void countDraw(uint32_t vertexCount, uint32_t instanceCount = 1) {
    if (auto* counts = RenderStatsCounting::active) {
        ++counts->drawCalls;
        counts->vertices += static_cast<uint64_t>(vertexCount) * instanceCount;
    }
}

/// Count a compute dispatch into the pass being recorded.
inline void countDispatch() {
    if (auto* counts = RenderStatsCounting::active) ++counts->dispatches;
}

// ═══════════════════════════════════════════════════════════════
// Statistics
// ═══════════════════════════════════════════════════════════════

struct PassStats {
    std::string name;
    /// Time spent recording the pass on the CPU.
    double      cpuMs = 0.0;
    /// GPU time from the end of the previous pass (or the start of the
    /// frame's graph work) to the end of this one. Passes overlap on the GPU,
    /// so this is the time the pass added; the passes of a frame sum to its
    /// GPU time.
    double      gpuMs = 0.0;
    bool        gpuValid = false;
    DrawCounts  draws;
};

struct FrameStats {
    /// Frame number counted from when stats were turned on; 0 for none.
    uint64_t   frame = 0;
    /// Recording the graph's passes on the CPU.
    double     cpuRecordMs = 0.0;
    /// From the start of the graph's GPU work to the end of its last pass.
    double     gpuMs = 0.0;
    bool       gpuValid = false;
    DrawCounts draws;
    /// Every pass that ran, disabled ones included, in execution order.
    std::vector<PassStats> passes;
};

struct RenderStats {
    // Over the last whole second
    double     fps = 0.0;
    double     frameTimeMs = 0.0;      ///< mean time between frames
    double     frameTimeMaxMs = 0.0;   ///< longest time between frames
    /// The frames of that second, averaged; draw counts are rounded.
    FrameStats average;

    /// The most recent frame whose GPU results have come back.
    FrameStats latest;

    /// Whether this device can time passes on the GPU, and GPU timing is on.
    bool       gpuTiming = false;

    /// Vulkan validation layers are on. They check every command recorded,
    /// which multiplies CPU recording time; profile without them.
    bool       validationLayers = false;
};

/// A readable summary: frame rate and times, totals, and the passes taking
/// the most GPU time (CPU time without GPU timing), at most `topPasses`.
std::string formatRenderStats(const RenderStats& stats, size_t topPasses = 8);

// ═══════════════════════════════════════════════════════════════
// Device-free parts, for tests
// ═══════════════════════════════════════════════════════════════

/// Collects frame intervals and finished frames, and every `windowMs` turns
/// them into the frame rate and averages of RenderStats.
class RenderStatsWindow {
public:
    explicit RenderStatsWindow(double windowMs = 1000.0) : m_windowMs(windowMs) {}

    /// Time since the previous frame began. Returns true when this closes a
    /// window and `out` was updated.
    bool addInterval(double ms, RenderStats& out);
    /// A frame whose GPU results are in; becomes `out.latest`.
    void addFrame(const FrameStats& frame, RenderStats& out);
    void reset();

private:
    double m_windowMs;
    double m_elapsedMs = 0.0;
    double m_maxMs = 0.0;
    uint32_t m_intervals = 0;

    // Sums of the frames that finished in this window
    uint32_t m_frames = 0;
    uint32_t m_gpuFrames = 0;
    uint32_t m_passFrames = 0;      // frames since the pass list last changed
    FrameStats m_sum;
    std::vector<uint32_t> m_passGpuFrames;
};

/// Fill the GPU times of `frame` from timestamps. `passQuery[i]` is the query
/// written after pass i and `prevQuery[i]` the one its time is measured
/// from; UINT32_MAX for none. `startQueries` mark where each command buffer's
/// graph work began. Timestamps not `available` leave the time invalid.
void resolveGpuTimes(FrameStats& frame,
                     const std::vector<uint64_t>& timestamps,
                     const std::vector<bool>& available,
                     const std::vector<uint32_t>& passQuery,
                     const std::vector<uint32_t>& prevQuery,
                     const std::vector<uint32_t>& startQueries,
                     double nsPerTick, uint64_t validMask);

// ═══════════════════════════════════════════════════════════════
// Collector
// ═══════════════════════════════════════════════════════════════

class RenderStatsCollector {
public:
    explicit RenderStatsCollector(VulkanDevice& device);
    ~RenderStatsCollector();   // the device must be idle

    RenderStatsCollector(const RenderStatsCollector&) = delete;
    RenderStatsCollector& operator=(const RenderStatsCollector&) = delete;

    /// GPU timing is on by default where the device supports it.
    void setGpuTiming(bool enabled);

    /// Called every second with fresh stats.
    void setWindowCallback(std::function<void(const RenderStats&)> callback) {
        m_onWindow = std::move(callback);
    }

    /// RenderGraph: a frame on frame-in-flight `slot` (of `slotCount`)
    /// begins, after the slot's fence was waited on. Collects what the slot's
    /// previous frame measured. `passCount` bounds the queries a frame needs.
    void beginFrame(uint32_t slot, uint32_t slotCount, uint32_t passCount);
    /// RenderGraph: the frame's passes are recorded.
    void endFrame();
    /// The graph was recompiled: frames in flight measured passes that no
    /// longer exist, so their results are dropped.
    void invalidate();

    /// FrameGraphExecutor: a command buffer starts recording passes.
    void beginRecording(VkCommandBuffer cmd);
    /// FrameGraphExecutor: around each pass. beginPass returns the counts
    /// draws should go to.
    DrawCounts* beginPass(const std::string& name);
    void endPass(VkCommandBuffer cmd);

    const RenderStats& stats() const { return m_stats; }

private:
    using Clock = std::chrono::steady_clock;

    struct Slot {
        FrameStats frame;
        size_t     passCount = 0;               // entries of frame.passes in use
        std::vector<uint32_t> passQuery, prevQuery, startQueries;
        uint32_t   queriesUsed = 0;
        uint32_t   lastQuery = UINT32_MAX;
        bool       pending = false;             // recorded, results not read
    };

    void ensurePool(uint32_t slotCount, uint32_t queriesPerSlot);
    void destroyPool();
    void collect(Slot& slot, uint32_t slotIndex);
    uint32_t writeTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);

    VulkanDevice& m_device;
    bool          m_gpuSupported = false;
    bool          m_gpuWanted = true;
    double        m_nsPerTick = 0.0;
    uint64_t      m_validMask = 0;

    VkQueryPool   m_pool = VK_NULL_HANDLE;
    uint32_t      m_poolSlots = 0;
    uint32_t      m_queriesPerSlot = 0;

    std::vector<Slot> m_slots;
    Slot*         m_current = nullptr;
    uint32_t      m_currentSlot = 0;
    uint64_t      m_frameCounter = 0;
    Clock::time_point m_lastBegin{};
    bool          m_haveLastBegin = false;
    Clock::time_point m_recordStart{};
    Clock::time_point m_passStart{};

    RenderStatsWindow m_window;
    RenderStats   m_stats;
    std::function<void(const RenderStats&)> m_onWindow;

    // Scratch for readback
    std::vector<uint64_t> m_results;
    std::vector<bool>     m_available;
};

} // namespace FrameGraph
} // namespace Shoonyakasha
