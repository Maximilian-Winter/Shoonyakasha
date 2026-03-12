//
// Shoonyakasha Engine - Frame Graph Debugger
//
// 朱雀司變  熾熱而速達
// The Vermilion Bird governs transformation — blazing heat, swift arrival
//

#include "Vulkan/FrameGraph/FrameGraphDebugger.h"
#include "Vulkan/VulkanDevice.h"
#include "Core/Logger.h"

#include <stdexcept>
#include <algorithm>
#include <numeric>

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════

FrameGraphDebugger::FrameGraphDebugger() {
    m_logger = new Logger("frame_graph_debugger.log");
    m_frameHistory.resize(MAX_FRAME_HISTORY);
}

FrameGraphDebugger::~FrameGraphDebugger() {
    // Note: GPU resources must be cleaned up externally with disableGpuTiming()
    delete m_logger;
}

// ═══════════════════════════════════════════════════════════════
// Enable/Disable Control
// ═══════════════════════════════════════════════════════════════

void FrameGraphDebugger::enable() {
    m_enabled = true;
    m_logger->log(LogLevel::Info, "Frame graph debugger enabled");
}

void FrameGraphDebugger::disable() {
    m_enabled = false;
    m_logger->log(LogLevel::Info, "Frame graph debugger disabled");
}

void FrameGraphDebugger::enableGpuTiming(VulkanDevice& device, uint32_t queryPoolSize) {
    if (m_gpuTimingEnabled) {
        m_logger->log(LogLevel::Warning, "GPU timing already enabled");
        return;
    }

    // Check if device supports timestamp queries
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device.getPhysicalDevice(), &props);

    if (props.limits.timestampComputeAndGraphics == VK_FALSE) {
        m_logger->log(LogLevel::Warning, "Device does not support timestamp queries");
        return;
    }

    m_timestampPeriod = props.limits.timestampPeriod;
    m_queryPoolSize = queryPoolSize;

    // Create query pool
    VkQueryPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    createInfo.queryCount = queryPoolSize;

    if (vkCreateQueryPool(device.getLogicalDevice(), &createInfo, nullptr, &m_timestampQueryPool) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create timestamp query pool");
        return;
    }

    m_gpuTimingEnabled = true;
    m_logger->log(LogLevel::Info, "GPU timing enabled (pool size: %u, period: %.2f ns)",
                  queryPoolSize, m_timestampPeriod);
}

void FrameGraphDebugger::disableGpuTiming() {
    // Note: Caller must ensure the query pool is not in use (device idle)
    // and provide the device to destroy the pool
    m_gpuTimingEnabled = false;
    m_timestampQueryPool = VK_NULL_HANDLE;
    m_queryPoolSize = 0;
    m_currentQueryIndex = 0;
}

// ═══════════════════════════════════════════════════════════════
// Execution Hooks
// ═══════════════════════════════════════════════════════════════

void FrameGraphDebugger::onFrameBegin(uint32_t frameIndex) {
    if (!m_enabled) return;

    m_currentFrameIndex = frameIndex;
    m_frameStartTime = std::chrono::high_resolution_clock::now();
    m_currentPassTimings.clear();
    m_currentExecutedPasses.clear();
    m_currentLayouts.clear();
    m_currentBarrierCount = 0;
    m_currentLayoutTransitions = 0;
    m_currentQueueTransfers = 0;
    m_passQueryIndices.clear();

    // Reset query counter for this frame
    m_currentQueryIndex = 0;

    emitEvent(DebugEventType::FrameBegin, "", "", "Frame " + std::to_string(frameIndex));
}

void FrameGraphDebugger::onFrameEnd(uint32_t frameIndex) {
    if (!m_enabled) return;

    // Evaluate assertions
    evaluateAssertions();

    // Store frame summary
    storeFrameSummary();

    emitEvent(DebugEventType::FrameEnd, "", "", "Frame " + std::to_string(frameIndex));
}

void FrameGraphDebugger::onPassBegin(uint32_t passIndex, const std::string& passName,
                                      VkCommandBuffer cmd) {
    if (!m_enabled) return;

    PassExecutionTiming timing;
    timing.passName = passName;
    timing.cpuStartMs = getCurrentTimeMs();

    m_currentPassTimings.push_back(timing);
    m_currentExecutedPasses.push_back(passName);

    // Write GPU timestamp if enabled
    if (m_gpuTimingEnabled && m_timestampQueryPool != VK_NULL_HANDLE) {
        if (m_currentQueryIndex + 2 <= m_queryPoolSize) {
            uint32_t startQuery = m_currentQueryIndex++;

            // Reset and write start timestamp
            vkCmdResetQueryPool(cmd, m_timestampQueryPool, startQuery, 1);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               m_timestampQueryPool, startQuery);

            m_passQueryIndices[passIndex] = {startQuery, 0}; // End query set later
        }
    }

    emitEvent(DebugEventType::PassBegin, passName);
}

void FrameGraphDebugger::onPassEnd(uint32_t passIndex, const std::string& passName,
                                    VkCommandBuffer cmd) {
    if (!m_enabled) return;

    // Update CPU timing
    for (auto& timing : m_currentPassTimings) {
        if (timing.passName == passName && timing.cpuEndMs == 0.0) {
            timing.cpuEndMs = getCurrentTimeMs();
            timing.cpuDurationMs = timing.cpuEndMs - timing.cpuStartMs;
            break;
        }
    }

    // Write GPU end timestamp if enabled
    if (m_gpuTimingEnabled && m_timestampQueryPool != VK_NULL_HANDLE) {
        auto it = m_passQueryIndices.find(passIndex);
        if (it != m_passQueryIndices.end() && m_currentQueryIndex < m_queryPoolSize) {
            uint32_t endQuery = m_currentQueryIndex++;

            vkCmdResetQueryPool(cmd, m_timestampQueryPool, endQuery, 1);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                               m_timestampQueryPool, endQuery);

            it->second.second = endQuery;
        }
    }

    emitEvent(DebugEventType::PassEnd, passName);
}

void FrameGraphDebugger::onBarrierInserted(const std::string& resourceName,
                                            VkImageLayout oldLayout, VkImageLayout newLayout,
                                            bool isQueueTransfer) {
    if (!m_enabled) return;

    m_currentBarrierCount++;

    if (oldLayout != newLayout) {
        m_currentLayoutTransitions++;
    }

    if (isQueueTransfer) {
        m_currentQueueTransfers++;
    }

    // Track current layout
    m_currentLayouts[resourceName] = newLayout;

    std::string details = "Layout: " + std::to_string(static_cast<int>(oldLayout)) +
                          " -> " + std::to_string(static_cast<int>(newLayout));
    if (isQueueTransfer) {
        details += " (queue transfer)";
    }

    emitEvent(DebugEventType::BarrierInserted, "", resourceName, details);
}

void FrameGraphDebugger::collectGpuTimings(VulkanDevice& device, uint32_t frameIndex) {
    if (!m_gpuTimingEnabled || m_timestampQueryPool == VK_NULL_HANDLE) return;
    if (m_passQueryIndices.empty()) return;

    // Read all timestamps at once
    std::vector<uint64_t> timestamps(m_currentQueryIndex);

    VkResult result = vkGetQueryPoolResults(
        device.getLogicalDevice(),
        m_timestampQueryPool,
        0, m_currentQueryIndex,
        timestamps.size() * sizeof(uint64_t),
        timestamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );

    if (result != VK_SUCCESS) {
        m_logger->log(LogLevel::Warning, "Failed to get timestamp query results");
        return;
    }

    // Update pass timings with GPU data
    for (auto& [passIndex, queryPair] : m_passQueryIndices) {
        uint32_t startQuery = queryPair.first;
        uint32_t endQuery = queryPair.second;

        if (endQuery == 0 || startQuery >= timestamps.size() || endQuery >= timestamps.size()) {
            continue;
        }

        uint64_t startTs = timestamps[startQuery];
        uint64_t endTs = timestamps[endQuery];

        // Convert to milliseconds
        double durationNs = static_cast<double>(endTs - startTs) * m_timestampPeriod;
        double durationMs = durationNs / 1000000.0;

        // Find the corresponding timing entry and update it
        // We need to match by pass index, which we stored in order
        size_t timingIdx = 0;
        for (auto& timing : m_currentPassTimings) {
            if (timingIdx < m_passQueryIndices.size()) {
                // Check if this is the right pass
                auto it = m_passQueryIndices.find(static_cast<uint32_t>(timingIdx));
                if (it != m_passQueryIndices.end() && it->first == passIndex) {
                    timing.gpuDurationMs = durationMs;
                    timing.gpuTimingValid = true;
                    break;
                }
            }
            timingIdx++;
        }
    }

    // Update the frame history with GPU timings
    if (m_totalFramesTracked > 0) {
        uint32_t historyIdx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;
        if (m_frameHistory[historyIdx].frameIndex == frameIndex) {
            m_frameHistory[historyIdx].passTimings = m_currentPassTimings;

            // Recalculate total GPU time
            double totalGpu = 0.0;
            for (const auto& timing : m_currentPassTimings) {
                if (timing.gpuTimingValid) {
                    totalGpu += timing.gpuDurationMs;
                }
            }
            m_frameHistory[historyIdx].totalGpuTimeMs = totalGpu;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Query API
// ═══════════════════════════════════════════════════════════════

std::vector<FrameExecutionSummary> FrameGraphDebugger::getRecentFrames(uint32_t count) const {
    std::vector<FrameExecutionSummary> result;

    uint32_t available = std::min(count, m_totalFramesTracked);
    available = std::min(available, MAX_FRAME_HISTORY);

    for (uint32_t i = 0; i < available; ++i) {
        uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1 - i) % MAX_FRAME_HISTORY;
        result.push_back(m_frameHistory[idx]);
    }

    return result;
}

std::optional<FrameExecutionSummary> FrameGraphDebugger::getFrameSummary(uint32_t frameIndex) const {
    for (uint32_t i = 0; i < std::min(m_totalFramesTracked, MAX_FRAME_HISTORY); ++i) {
        uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1 - i) % MAX_FRAME_HISTORY;
        if (m_frameHistory[idx].frameIndex == frameIndex) {
            return m_frameHistory[idx];
        }
    }
    return std::nullopt;
}

bool FrameGraphDebugger::wasPassExecuted(const std::string& passName) const {
    if (m_totalFramesTracked == 0) return false;

    uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;
    return m_frameHistory[idx].wasPassExecuted(passName);
}

double FrameGraphDebugger::getPassCpuTime(const std::string& passName) const {
    if (m_totalFramesTracked == 0) return 0.0;

    uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;
    return m_frameHistory[idx].getPassCpuTime(passName);
}

double FrameGraphDebugger::getPassGpuTime(const std::string& passName) const {
    if (m_totalFramesTracked == 0) return 0.0;

    uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;
    return m_frameHistory[idx].getPassGpuTime(passName);
}

std::string FrameGraphDebugger::getSlowestPass() const {
    if (m_totalFramesTracked == 0) return "";

    uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;
    return m_frameHistory[idx].getSlowestPass();
}

double FrameGraphDebugger::getFrameTime() const {
    if (m_totalFramesTracked == 0) return 0.0;

    uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;
    return m_frameHistory[idx].totalCpuTimeMs;
}

double FrameGraphDebugger::getPassAverageTime(const std::string& passName, uint32_t sampleCount) const {
    uint32_t available = std::min(sampleCount, m_totalFramesTracked);
    available = std::min(available, MAX_FRAME_HISTORY);

    if (available == 0) return 0.0;

    double total = 0.0;
    uint32_t validSamples = 0;

    for (uint32_t i = 0; i < available; ++i) {
        uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1 - i) % MAX_FRAME_HISTORY;
        double time = m_frameHistory[idx].getPassCpuTime(passName);
        if (time > 0.0) {
            total += time;
            validSamples++;
        }
    }

    return (validSamples > 0) ? (total / validSamples) : 0.0;
}

double FrameGraphDebugger::getAverageFrameTime(uint32_t sampleCount) const {
    uint32_t available = std::min(sampleCount, m_totalFramesTracked);
    available = std::min(available, MAX_FRAME_HISTORY);

    if (available == 0) return 0.0;

    double total = 0.0;

    for (uint32_t i = 0; i < available; ++i) {
        uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1 - i) % MAX_FRAME_HISTORY;
        total += m_frameHistory[idx].totalCpuTimeMs;
    }

    return total / available;
}

VkImageLayout FrameGraphDebugger::getResourceLayout(const std::string& resourceName) const {
    if (m_totalFramesTracked == 0) return VK_IMAGE_LAYOUT_UNDEFINED;

    uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;
    auto it = m_frameHistory[idx].finalLayouts.find(resourceName);
    if (it != m_frameHistory[idx].finalLayouts.end()) {
        return it->second;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

std::unordered_map<std::string, VkImageLayout> FrameGraphDebugger::getAllResourceLayouts() const {
    if (m_totalFramesTracked == 0) return {};

    uint32_t idx = (m_historyWriteIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;
    return m_frameHistory[idx].finalLayouts;
}

// ═══════════════════════════════════════════════════════════════
// Assertions
// ═══════════════════════════════════════════════════════════════

void FrameGraphDebugger::assertPassExecutes(const std::string& passName) {
    std::lock_guard<std::mutex> lock(m_assertionMutex);

    DebugAssertion assertion;
    assertion.type = DebugAssertion::Type::PassExecutes;
    assertion.passName = passName;
    m_assertions.push_back(assertion);
}

void FrameGraphDebugger::assertPassCulled(const std::string& passName) {
    std::lock_guard<std::mutex> lock(m_assertionMutex);

    DebugAssertion assertion;
    assertion.type = DebugAssertion::Type::PassCulled;
    assertion.passName = passName;
    m_assertions.push_back(assertion);
}

void FrameGraphDebugger::assertResourceLayout(const std::string& resourceName, VkImageLayout layout) {
    std::lock_guard<std::mutex> lock(m_assertionMutex);

    DebugAssertion assertion;
    assertion.type = DebugAssertion::Type::ResourceLayout;
    assertion.resourceName = resourceName;
    assertion.expectedLayout = layout;
    m_assertions.push_back(assertion);
}

void FrameGraphDebugger::assertPassOrder(const std::string& beforePass, const std::string& afterPass) {
    std::lock_guard<std::mutex> lock(m_assertionMutex);

    DebugAssertion assertion;
    assertion.type = DebugAssertion::Type::PassOrder;
    assertion.passName = beforePass;
    assertion.passNameB = afterPass;
    m_assertions.push_back(assertion);
}

void FrameGraphDebugger::clearAssertions() {
    std::lock_guard<std::mutex> lock(m_assertionMutex);
    m_assertions.clear();
}

std::vector<DebugAssertion> FrameGraphDebugger::getAssertionResults() const {
    std::lock_guard<std::mutex> lock(m_assertionMutex);
    return m_assertions;
}

bool FrameGraphDebugger::allAssertionsPassed() const {
    std::lock_guard<std::mutex> lock(m_assertionMutex);

    for (const auto& assertion : m_assertions) {
        if (assertion.triggered && !assertion.passed) {
            return false;
        }
    }
    return true;
}

void FrameGraphDebugger::validateAssertions() const {
    std::lock_guard<std::mutex> lock(m_assertionMutex);

    for (const auto& assertion : m_assertions) {
        if (assertion.triggered && !assertion.passed) {
            throw std::runtime_error("Frame graph assertion failed: " + assertion.failureMessage);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Event Callbacks
// ═══════════════════════════════════════════════════════════════

void FrameGraphDebugger::setEventCallback(DebugEventCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_eventCallback = std::move(callback);
}

void FrameGraphDebugger::clearEventCallback() {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_eventCallback = nullptr;
}

// ═══════════════════════════════════════════════════════════════
// Statistics
// ═══════════════════════════════════════════════════════════════

void FrameGraphDebugger::resetStatistics() {
    m_totalFramesTracked = 0;
    m_historyWriteIndex = 0;

    for (auto& frame : m_frameHistory) {
        frame = FrameExecutionSummary{};
    }
}

// ═══════════════════════════════════════════════════════════════
// Internal Helpers
// ═══════════════════════════════════════════════════════════════

void FrameGraphDebugger::emitEvent(DebugEventType type, const std::string& passName,
                                    const std::string& resourceName, const std::string& details) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);

    if (!m_eventCallback) return;

    DebugEvent event;
    event.type = type;
    event.frameIndex = m_currentFrameIndex;
    event.passName = passName;
    event.resourceName = resourceName;
    event.details = details;
    event.timestampMs = getCurrentTimeMs();

    m_eventCallback(event);
}

void FrameGraphDebugger::evaluateAssertions() {
    std::lock_guard<std::mutex> lock(m_assertionMutex);

    for (auto& assertion : m_assertions) {
        assertion.triggered = true;

        switch (assertion.type) {
            case DebugAssertion::Type::PassExecutes: {
                bool executed = false;
                for (const auto& name : m_currentExecutedPasses) {
                    if (name == assertion.passName) {
                        executed = true;
                        break;
                    }
                }
                assertion.passed = executed;
                if (!executed) {
                    assertion.failureMessage = "Pass '" + assertion.passName + "' was expected to execute but was culled";
                }
                break;
            }

            case DebugAssertion::Type::PassCulled: {
                bool executed = false;
                for (const auto& name : m_currentExecutedPasses) {
                    if (name == assertion.passName) {
                        executed = true;
                        break;
                    }
                }
                assertion.passed = !executed;
                if (executed) {
                    assertion.failureMessage = "Pass '" + assertion.passName + "' was expected to be culled but executed";
                }
                break;
            }

            case DebugAssertion::Type::ResourceLayout: {
                auto it = m_currentLayouts.find(assertion.resourceName);
                if (it == m_currentLayouts.end()) {
                    assertion.passed = false;
                    assertion.failureMessage = "Resource '" + assertion.resourceName + "' has no tracked layout";
                } else if (it->second != assertion.expectedLayout) {
                    assertion.passed = false;
                    assertion.failureMessage = "Resource '" + assertion.resourceName +
                        "' layout mismatch: expected " + std::to_string(static_cast<int>(assertion.expectedLayout)) +
                        ", got " + std::to_string(static_cast<int>(it->second));
                } else {
                    assertion.passed = true;
                }
                break;
            }

            case DebugAssertion::Type::PassOrder: {
                int beforeIdx = -1, afterIdx = -1;
                for (size_t i = 0; i < m_currentExecutedPasses.size(); ++i) {
                    if (m_currentExecutedPasses[i] == assertion.passName) beforeIdx = static_cast<int>(i);
                    if (m_currentExecutedPasses[i] == assertion.passNameB) afterIdx = static_cast<int>(i);
                }

                if (beforeIdx == -1) {
                    assertion.passed = false;
                    assertion.failureMessage = "Pass '" + assertion.passName + "' was not executed";
                } else if (afterIdx == -1) {
                    assertion.passed = false;
                    assertion.failureMessage = "Pass '" + assertion.passNameB + "' was not executed";
                } else if (beforeIdx >= afterIdx) {
                    assertion.passed = false;
                    assertion.failureMessage = "Pass '" + assertion.passName +
                        "' did not execute before '" + assertion.passNameB + "'";
                } else {
                    assertion.passed = true;
                }
                break;
            }
        }
    }
}

void FrameGraphDebugger::storeFrameSummary() {
    FrameExecutionSummary summary;
    summary.frameIndex = m_currentFrameIndex;
    summary.passTimings = m_currentPassTimings;
    summary.executedPasses = m_currentExecutedPasses;
    summary.finalLayouts = m_currentLayouts;
    summary.totalBarriers = m_currentBarrierCount;
    summary.layoutTransitions = m_currentLayoutTransitions;
    summary.queueTransfers = m_currentQueueTransfers;

    // Calculate total CPU time
    auto now = std::chrono::high_resolution_clock::now();
    summary.totalCpuTimeMs = std::chrono::duration<double, std::milli>(now - m_frameStartTime).count();

    // Calculate total GPU time (if available)
    double totalGpu = 0.0;
    for (const auto& timing : m_currentPassTimings) {
        if (timing.gpuTimingValid) {
            totalGpu += timing.gpuDurationMs;
        }
    }
    summary.totalGpuTimeMs = totalGpu;

    // Store in circular buffer
    m_frameHistory[m_historyWriteIndex] = std::move(summary);
    m_historyWriteIndex = (m_historyWriteIndex + 1) % MAX_FRAME_HISTORY;
    m_totalFramesTracked++;
}

double FrameGraphDebugger::getCurrentTimeMs() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(now - m_frameStartTime).count();
}

} // namespace FrameGraph
} // namespace Shoonyakasha
