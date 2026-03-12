//
// Shoonyakasha Engine - Frame Graph Debugger
//
// 朱雀司變  熾熱而速達
// The Vermilion Bird governs transformation — blazing heat, swift arrival
//
// Runtime debugging, pass execution tracking, and GPU timing.
//

#pragma once

#include "FrameGraphPass.h"
#include "FrameGraphResource.h"

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <chrono>
#include <optional>
#include <mutex>

namespace Shoonyakasha {
class VulkanDevice;
class Logger;
}

namespace Shoonyakasha {
namespace FrameGraph {

// Forward declarations
class FrameGraphBuilder;
struct BarrierInfo;

// ═══════════════════════════════════════════════════════════════
// Debug Event Types
// ═══════════════════════════════════════════════════════════════

enum class DebugEventType {
    PassBegin,
    PassEnd,
    BarrierInserted,
    FrameBegin,
    FrameEnd
};

struct DebugEvent {
    DebugEventType  type;
    uint32_t        frameIndex;
    std::string     passName;
    std::string     resourceName;   // For barrier events
    std::string     details;        // Additional info
    double          timestampMs;    // Time since frame start
};

using DebugEventCallback = std::function<void(const DebugEvent&)>;

// ═══════════════════════════════════════════════════════════════
// Pass Execution Timing
// ═══════════════════════════════════════════════════════════════

struct PassExecutionTiming {
    std::string passName;
    double      cpuStartMs = 0.0;      // CPU time when pass began recording
    double      cpuEndMs = 0.0;        // CPU time when pass finished recording
    double      cpuDurationMs = 0.0;   // CPU-side duration

    // GPU timing (only if GPU timing enabled)
    double      gpuDurationMs = 0.0;   // GPU-side duration (0 if not available)
    bool        gpuTimingValid = false;
};

// ═══════════════════════════════════════════════════════════════
// Frame Execution Summary
// ═══════════════════════════════════════════════════════════════

struct FrameExecutionSummary {
    uint32_t    frameIndex = 0;
    double      totalCpuTimeMs = 0.0;
    double      totalGpuTimeMs = 0.0;       // 0 if GPU timing not enabled

    std::vector<PassExecutionTiming>        passTimings;
    std::vector<std::string>                executedPasses;

    // Final layout state of resources (for debugging layout issues)
    std::unordered_map<std::string, VkImageLayout> finalLayouts;

    // Barrier statistics
    uint32_t    totalBarriers = 0;
    uint32_t    layoutTransitions = 0;
    uint32_t    queueTransfers = 0;

    // Quick queries
    bool wasPassExecuted(const std::string& passName) const {
        for (const auto& name : executedPasses) {
            if (name == passName) return true;
        }
        return false;
    }

    double getPassCpuTime(const std::string& passName) const {
        for (const auto& timing : passTimings) {
            if (timing.passName == passName) return timing.cpuDurationMs;
        }
        return 0.0;
    }

    double getPassGpuTime(const std::string& passName) const {
        for (const auto& timing : passTimings) {
            if (timing.passName == passName && timing.gpuTimingValid) {
                return timing.gpuDurationMs;
            }
        }
        return 0.0;
    }

    std::string getSlowestPass() const {
        std::string slowest;
        double maxTime = 0.0;
        for (const auto& timing : passTimings) {
            double time = timing.gpuTimingValid ? timing.gpuDurationMs : timing.cpuDurationMs;
            if (time > maxTime) {
                maxTime = time;
                slowest = timing.passName;
            }
        }
        return slowest;
    }
};

// ═══════════════════════════════════════════════════════════════
// Debug Assertion
// ═══════════════════════════════════════════════════════════════

struct DebugAssertion {
    enum class Type {
        PassExecutes,       // Assert that a specific pass executes
        PassCulled,         // Assert that a specific pass is culled
        ResourceLayout,     // Assert that a resource ends in a specific layout
        PassOrder           // Assert that passA executes before passB
    };

    Type        type;
    std::string passName;
    std::string passNameB;              // For PassOrder assertions
    std::string resourceName;           // For ResourceLayout assertions
    VkImageLayout expectedLayout;       // For ResourceLayout assertions

    bool        triggered = false;      // Was this assertion evaluated?
    bool        passed = false;         // Did it pass?
    std::string failureMessage;
};

// ═══════════════════════════════════════════════════════════════
// Frame Graph Debugger Class
// 朱雀之焰煉其精純 — The Vermilion Bird's flame refines to purity
// ═══════════════════════════════════════════════════════════════

class FrameGraphDebugger {
public:
    FrameGraphDebugger();
    ~FrameGraphDebugger();

    // ═══════════════════════════════════════════════════════════════
    // Enable/Disable Control
    // ═══════════════════════════════════════════════════════════════

    void enable();
    void disable();
    bool isEnabled() const { return m_enabled; }

    // Enable GPU timestamp queries (requires device support)
    // queryPoolSize = max number of timestamp queries (2 per pass)
    void enableGpuTiming(VulkanDevice& device, uint32_t queryPoolSize = 256);
    void disableGpuTiming();
    bool isGpuTimingEnabled() const { return m_gpuTimingEnabled; }

    // ═══════════════════════════════════════════════════════════════
    // Execution Hooks (called by FrameGraphExecutor)
    // ═══════════════════════════════════════════════════════════════

    void onFrameBegin(uint32_t frameIndex);
    void onFrameEnd(uint32_t frameIndex);

    void onPassBegin(uint32_t passIndex, const std::string& passName,
                     VkCommandBuffer cmd);
    void onPassEnd(uint32_t passIndex, const std::string& passName,
                   VkCommandBuffer cmd);

    void onBarrierInserted(const std::string& resourceName,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           bool isQueueTransfer);

    // Called after GPU work completes to read timestamp results
    void collectGpuTimings(VulkanDevice& device, uint32_t frameIndex);

    // ═══════════════════════════════════════════════════════════════
    // Query API
    // ═══════════════════════════════════════════════════════════════

    // Get summaries for recent frames (most recent first)
    std::vector<FrameExecutionSummary> getRecentFrames(uint32_t count = 10) const;

    // Get summary for a specific frame
    std::optional<FrameExecutionSummary> getFrameSummary(uint32_t frameIndex) const;

    // Quick queries for the most recent frame
    bool wasPassExecuted(const std::string& passName) const;
    double getPassCpuTime(const std::string& passName) const;
    double getPassGpuTime(const std::string& passName) const;
    std::string getSlowestPass() const;
    double getFrameTime() const;

    // Get average time over recent frames
    double getPassAverageTime(const std::string& passName, uint32_t sampleCount = 60) const;
    double getAverageFrameTime(uint32_t sampleCount = 60) const;

    // Resource layout tracking
    VkImageLayout getResourceLayout(const std::string& resourceName) const;
    std::unordered_map<std::string, VkImageLayout> getAllResourceLayouts() const;

    // ═══════════════════════════════════════════════════════════════
    // Assertions (for testing and validation)
    // ═══════════════════════════════════════════════════════════════

    // Add assertions (checked at frame end)
    void assertPassExecutes(const std::string& passName);
    void assertPassCulled(const std::string& passName);
    void assertResourceLayout(const std::string& resourceName, VkImageLayout layout);
    void assertPassOrder(const std::string& beforePass, const std::string& afterPass);

    // Clear all assertions
    void clearAssertions();

    // Get assertion results (after frame completes)
    std::vector<DebugAssertion> getAssertionResults() const;
    bool allAssertionsPassed() const;

    // Throw exception if any assertion fails (useful for tests)
    void validateAssertions() const;

    // ═══════════════════════════════════════════════════════════════
    // Event Callbacks
    // ═══════════════════════════════════════════════════════════════

    void setEventCallback(DebugEventCallback callback);
    void clearEventCallback();

    // ═══════════════════════════════════════════════════════════════
    // Statistics
    // ═══════════════════════════════════════════════════════════════

    uint32_t getTotalFramesTracked() const { return m_totalFramesTracked; }
    void resetStatistics();

private:
    bool                        m_enabled = false;
    bool                        m_gpuTimingEnabled = false;

    // GPU timing resources
    VkQueryPool                 m_timestampQueryPool = VK_NULL_HANDLE;
    uint32_t                    m_queryPoolSize = 0;
    uint32_t                    m_currentQueryIndex = 0;
    float                       m_timestampPeriod = 0.0f;  // ns per timestamp unit

    // Current frame state
    uint32_t                    m_currentFrameIndex = 0;
    std::chrono::high_resolution_clock::time_point m_frameStartTime;
    std::vector<PassExecutionTiming> m_currentPassTimings;
    std::vector<std::string>    m_currentExecutedPasses;
    std::unordered_map<std::string, VkImageLayout> m_currentLayouts;
    uint32_t                    m_currentBarrierCount = 0;
    uint32_t                    m_currentLayoutTransitions = 0;
    uint32_t                    m_currentQueueTransfers = 0;

    // Pass index -> (startQueryIndex, endQueryIndex) for GPU timing
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> m_passQueryIndices;

    // History (circular buffer of recent frames)
    static constexpr uint32_t   MAX_FRAME_HISTORY = 120;  // 2 seconds at 60fps
    std::vector<FrameExecutionSummary> m_frameHistory;
    uint32_t                    m_historyWriteIndex = 0;
    uint32_t                    m_totalFramesTracked = 0;

    // Assertions
    std::vector<DebugAssertion> m_assertions;
    mutable std::mutex          m_assertionMutex;

    // Event callback
    DebugEventCallback          m_eventCallback;
    mutable std::mutex          m_callbackMutex;

    // Logging
    Logger*                     m_logger = nullptr;

    // Internal helpers
    void emitEvent(DebugEventType type, const std::string& passName = "",
                   const std::string& resourceName = "", const std::string& details = "");
    void evaluateAssertions();
    void storeFrameSummary();
    double getCurrentTimeMs() const;
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
