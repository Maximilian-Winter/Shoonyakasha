//
// Shoonyakasha Engine - Render Statistics
//

#include "Vulkan/FrameGraph/RenderStats.h"
#include "Vulkan/VulkanDevice.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace Shoonyakasha {
namespace FrameGraph {

namespace {

constexpr uint32_t kNoQuery = UINT32_MAX;

/// Extra queries per frame beyond one per pass: a start timestamp for each
/// command buffer the passes are recorded into.
constexpr uint32_t kStartQueries = 8;

double msSince(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::string formatCount(uint64_t n) {
    char buffer[32];
    if (n >= 10'000'000)      std::snprintf(buffer, sizeof buffer, "%.1fM", n / 1e6);
    else if (n >= 10'000)     std::snprintf(buffer, sizeof buffer, "%.1fk", n / 1e3);
    else                      std::snprintf(buffer, sizeof buffer, "%llu", static_cast<unsigned long long>(n));
    return buffer;
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// Window
// ═══════════════════════════════════════════════════════════════

void RenderStatsWindow::reset() {
    m_elapsedMs = 0.0;
    m_maxMs = 0.0;
    m_intervals = 0;
    m_frames = 0;
    m_gpuFrames = 0;
    m_passFrames = 0;
    m_sum = FrameStats{};
    m_passGpuFrames.clear();
}

void RenderStatsWindow::addFrame(const FrameStats& frame, RenderStats& out) {
    out.latest = frame;

    ++m_frames;
    m_sum.frame = frame.frame;
    m_sum.cpuRecordMs += frame.cpuRecordMs;
    m_sum.draws += frame.draws;
    if (frame.gpuValid) {
        m_sum.gpuMs += frame.gpuMs;
        ++m_gpuFrames;
    }

    // Per pass, as long as the frames have the same passes.
    bool same = m_sum.passes.size() == frame.passes.size();
    for (size_t i = 0; same && i < frame.passes.size(); ++i) {
        same = m_sum.passes[i].name == frame.passes[i].name;
    }
    if (!same) {
        m_sum.passes.assign(frame.passes.size(), PassStats{});
        for (size_t i = 0; i < frame.passes.size(); ++i) m_sum.passes[i].name = frame.passes[i].name;
        m_passGpuFrames.assign(frame.passes.size(), 0);
        m_passFrames = 0;
    }
    ++m_passFrames;
    for (size_t i = 0; i < frame.passes.size(); ++i) {
        auto& sum = m_sum.passes[i];
        const auto& pass = frame.passes[i];
        sum.cpuMs += pass.cpuMs;
        sum.draws += pass.draws;
        if (pass.gpuValid) {
            sum.gpuMs += pass.gpuMs;
            ++m_passGpuFrames[i];
        }
    }
}

bool RenderStatsWindow::addInterval(double ms, RenderStats& out) {
    m_elapsedMs += ms;
    m_maxMs = std::max(m_maxMs, ms);
    ++m_intervals;
    if (m_elapsedMs < m_windowMs) return false;

    out.fps = m_intervals * 1000.0 / m_elapsedMs;
    out.frameTimeMs = m_elapsedMs / m_intervals;
    out.frameTimeMaxMs = m_maxMs;

    if (m_frames > 0) {
        auto mean = [](uint64_t sum, uint32_t n) { return std::llround(static_cast<double>(sum) / n); };
        auto& avg = out.average;
        avg.frame = m_sum.frame;
        avg.cpuRecordMs = m_sum.cpuRecordMs / m_frames;
        avg.gpuValid = m_gpuFrames > 0;
        avg.gpuMs = avg.gpuValid ? m_sum.gpuMs / m_gpuFrames : 0.0;
        avg.draws.drawCalls  = static_cast<uint32_t>(mean(m_sum.draws.drawCalls, m_frames));
        avg.draws.dispatches = static_cast<uint32_t>(mean(m_sum.draws.dispatches, m_frames));
        avg.draws.vertices   = static_cast<uint64_t>(mean(m_sum.draws.vertices, m_frames));

        avg.passes.resize(m_sum.passes.size());
        for (size_t i = 0; i < m_sum.passes.size(); ++i) {
            const auto& sum = m_sum.passes[i];
            auto& pass = avg.passes[i];
            pass.name = sum.name;
            pass.cpuMs = sum.cpuMs / m_passFrames;
            pass.gpuValid = m_passGpuFrames[i] > 0;
            pass.gpuMs = pass.gpuValid ? sum.gpuMs / m_passGpuFrames[i] : 0.0;
            pass.draws.drawCalls  = static_cast<uint32_t>(mean(sum.draws.drawCalls, m_passFrames));
            pass.draws.dispatches = static_cast<uint32_t>(mean(sum.draws.dispatches, m_passFrames));
            pass.draws.vertices   = static_cast<uint64_t>(mean(sum.draws.vertices, m_passFrames));
        }
    }

    // Start the next window, keeping the pass list so it is not rebuilt.
    m_elapsedMs = 0.0;
    m_maxMs = 0.0;
    m_intervals = 0;
    m_frames = 0;
    m_gpuFrames = 0;
    m_passFrames = 0;
    m_sum.cpuRecordMs = 0.0;
    m_sum.gpuMs = 0.0;
    m_sum.draws = {};
    for (auto& pass : m_sum.passes) {
        pass.cpuMs = 0.0;
        pass.gpuMs = 0.0;
        pass.draws = {};
    }
    std::fill(m_passGpuFrames.begin(), m_passGpuFrames.end(), 0u);
    return true;
}

// ═══════════════════════════════════════════════════════════════
// GPU times
// ═══════════════════════════════════════════════════════════════

void resolveGpuTimes(FrameStats& frame,
                     const std::vector<uint64_t>& timestamps,
                     const std::vector<bool>& available,
                     const std::vector<uint32_t>& passQuery,
                     const std::vector<uint32_t>& prevQuery,
                     const std::vector<uint32_t>& startQueries,
                     double nsPerTick, uint64_t validMask)
{
    auto ok = [&](uint32_t q) { return q < timestamps.size() && q < available.size() && available[q]; };
    // Ticks from a to b, wrapping within the valid bits; negative when b is
    // earlier (another command buffer's start, say).
    auto ticks = [&](uint32_t a, uint32_t b) -> double {
        const uint64_t diff = (timestamps[b] - timestamps[a]) & validMask;
        if (diff > validMask / 2) return -static_cast<double>((validMask - diff) + 1);
        return static_cast<double>(diff);
    };
    auto toMs = [&](double t) { return t * nsPerTick / 1e6; };

    for (size_t i = 0; i < frame.passes.size(); ++i) {
        auto& pass = frame.passes[i];
        pass.gpuValid = false;
        pass.gpuMs = 0.0;
        if (i >= passQuery.size() || i >= prevQuery.size()) continue;
        if (!ok(passQuery[i]) || !ok(prevQuery[i])) continue;
        pass.gpuMs = std::max(0.0, toMs(ticks(prevQuery[i], passQuery[i])));
        pass.gpuValid = true;
    }

    // The frame: earliest start to latest pass end, relative to one start.
    frame.gpuValid = false;
    frame.gpuMs = 0.0;
    uint32_t origin = kNoQuery;
    for (uint32_t q : startQueries) {
        if (ok(q)) { origin = q; break; }
    }
    if (origin == kNoQuery) return;
    double first = 0.0, last = 0.0;
    bool anyEnd = false;
    for (uint32_t q : startQueries) {
        if (ok(q)) first = std::min(first, ticks(origin, q));
    }
    for (uint32_t q : passQuery) {
        if (!ok(q)) continue;
        const double t = ticks(origin, q);
        last = anyEnd ? std::max(last, t) : t;
        anyEnd = true;
    }
    if (!anyEnd) return;
    frame.gpuMs = std::max(0.0, toMs(last - first));
    frame.gpuValid = true;
}

// ═══════════════════════════════════════════════════════════════
// Summary
// ═══════════════════════════════════════════════════════════════

std::string formatRenderStats(const RenderStats& stats, size_t topPasses) {
    const FrameStats& f = stats.average;
    char line[256];
    std::string out;

    std::snprintf(line, sizeof line, "%.1f FPS | frame %.2f ms (max %.2f) | CPU record %.2f ms | GPU ",
                  stats.fps, stats.frameTimeMs, stats.frameTimeMaxMs, f.cpuRecordMs);
    out += line;
    if (f.gpuValid) {
        std::snprintf(line, sizeof line, "%.2f ms", f.gpuMs);
        out += line;
    } else {
        out += stats.gpuTiming ? "pending" : "off";
    }
    out += " | " + std::to_string(f.draws.drawCalls) + " draws, " + std::to_string(f.draws.dispatches) +
           " dispatches, " + formatCount(f.draws.vertices) + " vertices\n";

    // The passes that cost the most.
    std::vector<size_t> order(f.passes.size());
    std::iota(order.begin(), order.end(), size_t{0});
    const bool byGpu = std::any_of(f.passes.begin(), f.passes.end(), [](const PassStats& p) { return p.gpuValid; });
    auto cost = [&](size_t i) { return byGpu ? f.passes[i].gpuMs : f.passes[i].cpuMs; };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return cost(a) > cost(b); });
    if (order.size() > topPasses) order.resize(topPasses);

    for (size_t i : order) {
        const auto& p = f.passes[i];
        if (p.gpuValid) std::snprintf(line, sizeof line, "  %-28s GPU %6.3f ms  CPU %6.3f ms  %5u draws  %s vertices\n",
                                      p.name.c_str(), p.gpuMs, p.cpuMs, p.draws.drawCalls,
                                      formatCount(p.draws.vertices).c_str());
        else            std::snprintf(line, sizeof line, "  %-28s CPU %6.3f ms  %5u draws  %s vertices\n",
                                      p.name.c_str(), p.cpuMs, p.draws.drawCalls,
                                      formatCount(p.draws.vertices).c_str());
        out += line;
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════
// Collector
// ═══════════════════════════════════════════════════════════════

RenderStatsCollector::RenderStatsCollector(VulkanDevice& device)
    : m_device(device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device.getPhysicalDevice(), &props);

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device.getPhysicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device.getPhysicalDevice(), &familyCount, families.data());
    const uint32_t family = device.getGraphicsQueueFamily();
    const uint32_t validBits = family < families.size() ? families[family].timestampValidBits : 0;

    m_gpuSupported = props.limits.timestampComputeAndGraphics == VK_TRUE && validBits > 0 &&
                     props.limits.timestampPeriod > 0.0f;
    m_nsPerTick = props.limits.timestampPeriod;
    m_validMask = validBits >= 64 ? ~uint64_t{0} : ((uint64_t{1} << validBits) - 1);
    m_stats.gpuTiming = m_gpuSupported && m_gpuWanted;
}

RenderStatsCollector::~RenderStatsCollector() {
    RenderStatsCounting::active = nullptr;
    destroyPool();
}

void RenderStatsCollector::setGpuTiming(bool enabled) {
    m_gpuWanted = enabled;
    m_stats.gpuTiming = m_gpuSupported && m_gpuWanted;
}

void RenderStatsCollector::destroyPool() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device.getLogicalDevice(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    m_poolSlots = 0;
    m_queriesPerSlot = 0;
}

void RenderStatsCollector::ensurePool(uint32_t slotCount, uint32_t queriesPerSlot) {
    if (m_pool != VK_NULL_HANDLE && slotCount <= m_poolSlots && queriesPerSlot <= m_queriesPerSlot) return;

    if (m_pool != VK_NULL_HANDLE) {
        // Frames in flight may still write the old pool. This happens only
        // when the graph gains passes, which recompiling already paused for.
        vkDeviceWaitIdle(m_device.getLogicalDevice());
        destroyPool();
        for (auto& slot : m_slots) slot.pending = false;
    }

    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = slotCount * queriesPerSlot;
    if (vkCreateQueryPool(m_device.getLogicalDevice(), &info, nullptr, &m_pool) != VK_SUCCESS) {
        m_pool = VK_NULL_HANDLE;
        return;
    }
    m_poolSlots = slotCount;
    m_queriesPerSlot = queriesPerSlot;
}

void RenderStatsCollector::collect(Slot& slot, uint32_t slotIndex) {
    slot.pending = false;

    const bool readable = m_pool != VK_NULL_HANDLE && slot.queriesUsed > 0 && slotIndex < m_poolSlots;
    if (readable) {
        // Value and availability per query. The slot's fence was waited on,
        // so everything it wrote is in; no wait.
        m_results.assign(static_cast<size_t>(slot.queriesUsed) * 2, 0);
        const VkResult result = vkGetQueryPoolResults(
            m_device.getLogicalDevice(), m_pool, slotIndex * m_queriesPerSlot, slot.queriesUsed,
            m_results.size() * sizeof(uint64_t), m_results.data(), 2 * sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        std::vector<uint64_t> timestamps(slot.queriesUsed);
        m_available.assign(slot.queriesUsed, false);
        if (result == VK_SUCCESS || result == VK_NOT_READY) {
            for (uint32_t q = 0; q < slot.queriesUsed; ++q) {
                timestamps[q] = m_results[2 * q];
                m_available[q] = m_results[2 * q + 1] != 0;
            }
        }
        resolveGpuTimes(slot.frame, timestamps, m_available, slot.passQuery, slot.prevQuery,
                        slot.startQueries, m_nsPerTick, m_validMask);
    } else {
        slot.frame.gpuValid = false;
        for (auto& pass : slot.frame.passes) pass.gpuValid = false;
    }

    m_window.addFrame(slot.frame, m_stats);
}

void RenderStatsCollector::beginFrame(uint32_t slot, uint32_t slotCount, uint32_t passCount) {
    const auto now = Clock::now();
    slotCount = std::max(slotCount, slot + 1);
    if (m_slots.size() < slotCount) m_slots.resize(slotCount);

    Slot& s = m_slots[slot];
    if (s.pending) collect(s, slot);

    if (m_haveLastBegin && m_window.addInterval(msSince(m_lastBegin, now), m_stats) && m_onWindow) {
        m_onWindow(m_stats);
    }
    m_lastBegin = now;
    m_haveLastBegin = true;

    if (m_stats.gpuTiming) ensurePool(slotCount, passCount + kStartQueries);

    s.frame.frame = ++m_frameCounter;
    s.frame.cpuRecordMs = 0.0;
    s.frame.gpuMs = 0.0;
    s.frame.gpuValid = false;
    s.frame.draws = {};
    s.passCount = 0;
    s.passQuery.clear();
    s.prevQuery.clear();
    s.startQueries.clear();
    s.queriesUsed = 0;
    s.lastQuery = kNoQuery;

    m_current = &s;
    m_currentSlot = slot;
    m_recordStart = now;
}

void RenderStatsCollector::endFrame() {
    if (!m_current) return;
    Slot& s = *m_current;
    s.frame.passes.resize(s.passCount);
    s.frame.cpuRecordMs = msSince(m_recordStart, Clock::now());
    s.frame.draws = {};
    for (const auto& pass : s.frame.passes) s.frame.draws += pass.draws;
    s.pending = true;
    m_current = nullptr;
}

void RenderStatsCollector::invalidate() {
    for (auto& slot : m_slots) slot.pending = false;
    m_current = nullptr;
    m_window.reset();
}

uint32_t RenderStatsCollector::writeTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage) {
    if (!m_current || !m_stats.gpuTiming || m_pool == VK_NULL_HANDLE) return kNoQuery;
    Slot& s = *m_current;
    if (m_currentSlot >= m_poolSlots || s.queriesUsed >= m_queriesPerSlot) return kNoQuery;

    const uint32_t local = s.queriesUsed++;
    const uint32_t query = m_currentSlot * m_queriesPerSlot + local;
    // Reset where it is written: passes may be split over several command
    // buffers, and this is always outside rendering.
    vkCmdResetQueryPool(cmd, m_pool, query, 1);
    vkCmdWriteTimestamp(cmd, stage, m_pool, query);
    return local;
}

void RenderStatsCollector::beginRecording(VkCommandBuffer cmd) {
    if (!m_current) return;
    const uint32_t q = writeTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    if (q != kNoQuery) m_current->startQueries.push_back(q);
    m_current->lastQuery = q;
}

DrawCounts* RenderStatsCollector::beginPass(const std::string& name) {
    if (!m_current) return nullptr;
    Slot& s = *m_current;
    // Entries are reused from frame to frame, names and all.
    if (s.passCount == s.frame.passes.size()) s.frame.passes.emplace_back();
    PassStats& pass = s.frame.passes[s.passCount++];
    pass.name = name;
    pass.cpuMs = 0.0;
    pass.gpuMs = 0.0;
    pass.gpuValid = false;
    pass.draws = {};
    s.passQuery.push_back(kNoQuery);
    s.prevQuery.push_back(kNoQuery);
    m_passStart = Clock::now();
    return &pass.draws;
}

void RenderStatsCollector::endPass(VkCommandBuffer cmd) {
    if (!m_current || m_current->passCount == 0) return;
    Slot& s = *m_current;
    const size_t index = s.passCount - 1;
    s.frame.passes[index].cpuMs = msSince(m_passStart, Clock::now());

    // After everything before it has finished: the pass's end.
    const uint32_t q = writeTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    if (q != kNoQuery) {
        s.passQuery[index] = q;
        s.prevQuery[index] = s.lastQuery;
        s.lastQuery = q;
    }
}

} // namespace FrameGraph
} // namespace Shoonyakasha
