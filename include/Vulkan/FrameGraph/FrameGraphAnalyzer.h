//
// Shoonyakasha Engine - Frame Graph Analyzer
//
// 玄武司察  深潛而見真
// The Dark Warrior investigates — diving deep to see truth
//
// Static analysis, validation, and insights for frame graphs.
//

#pragma once

#include "FrameGraphPass.h"
#include "FrameGraphResource.h"

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace Shoonyakasha {
class Logger;
}

namespace Shoonyakasha {
namespace FrameGraph {

// Forward declarations
class FrameGraphBuilder;
class FrameGraphCompiler;
struct CompiledPass;
struct BarrierInfo;
// Note: PhysicalResource is a type alias (std::variant) defined in FrameGraph.h
// and cannot be forward declared. Template methods use it via CompileResult.

// ═══════════════════════════════════════════════════════════════
// Analysis Severity Levels
// ═══════════════════════════════════════════════════════════════

enum class AnalysisSeverity {
    Info,       // Informational insight (statistics, facts)
    Suggestion, // Optimization opportunity
    Warning,    // Potential issue that may cause problems
    Error       // Configuration problem that will cause failures
};

// ═══════════════════════════════════════════════════════════════
// Analysis Finding — a single insight or issue
// ═══════════════════════════════════════════════════════════════

struct AnalysisFinding {
    AnalysisSeverity severity = AnalysisSeverity::Info;
    std::string      category;      // "culling", "barrier", "binding", "dependency", "memory"
    std::string      passName;      // Empty if graph-wide finding
    std::string      resourceName;  // Empty if not resource-specific
    std::string      message;       // Human-readable description
    std::string      suggestion;    // Actionable fix (empty if just informational)

    // Helper constructors
    static AnalysisFinding info(const std::string& cat, const std::string& msg) {
        return {AnalysisSeverity::Info, cat, "", "", msg, ""};
    }
    static AnalysisFinding warning(const std::string& cat, const std::string& msg, const std::string& fix = "") {
        return {AnalysisSeverity::Warning, cat, "", "", msg, fix};
    }
    static AnalysisFinding error(const std::string& cat, const std::string& msg, const std::string& fix = "") {
        return {AnalysisSeverity::Error, cat, "", "", msg, fix};
    }
};

// ═══════════════════════════════════════════════════════════════
// Culling Report — why passes were removed
// 知病之所在  方可施藥
// Know where the illness lies, then apply medicine
// ═══════════════════════════════════════════════════════════════

struct CullingReport {
    // Pass names that were removed during dead pass culling
    std::vector<std::string> culledPasses;

    // Pass names that survived (in execution order)
    std::vector<std::string> livePasses;

    // WHY each pass was culled (passName -> reason)
    // Possible reasons:
    //   - "No outputs consumed by any live pass"
    //   - "All output consumers were also culled"
    //   - "Outputs only imported resources with no further use"
    std::unordered_map<std::string, std::string> cullReasons;

    // Dependency chains showing cascade effects
    // For each culled pass, lists passes that depended on it and were also culled
    std::unordered_map<std::string, std::vector<std::string>> cascadeEffects;

    // Quick stats
    uint32_t totalDeclared() const { return static_cast<uint32_t>(culledPasses.size() + livePasses.size()); }
    uint32_t cullCount() const { return static_cast<uint32_t>(culledPasses.size()); }
    uint32_t liveCount() const { return static_cast<uint32_t>(livePasses.size()); }
    bool hasCulledPasses() const { return !culledPasses.empty(); }
};

// ═══════════════════════════════════════════════════════════════
// Barrier Analysis — layout transitions and synchronization
// ═══════════════════════════════════════════════════════════════

struct BarrierAnalysis {
    std::string resourceName;
    std::string passName;           // Pass that requires this barrier (before execution)

    // Layout transition (human-readable strings)
    std::string oldLayoutStr;       // e.g., "COLOR_ATTACHMENT_OPTIMAL"
    std::string newLayoutStr;       // e.g., "SHADER_READ_ONLY_OPTIMAL"

    // Pipeline stages
    std::string srcStageStr;        // e.g., "COLOR_ATTACHMENT_OUTPUT"
    std::string dstStageStr;        // e.g., "FRAGMENT_SHADER"

    // Access masks
    std::string srcAccessStr;       // e.g., "COLOR_ATTACHMENT_WRITE"
    std::string dstAccessStr;       // e.g., "SHADER_READ"

    // Raw Vulkan values (for programmatic use)
    VkImageLayout           oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout           newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags    srcStage = 0;
    VkPipelineStageFlags    dstStage = 0;
    VkAccessFlags           srcAccess = 0;
    VkAccessFlags           dstAccess = 0;

    // Cross-queue info
    bool        isQueueOwnershipTransfer = false;
    std::string srcQueueType;       // "graphics" or "compute"
    std::string dstQueueType;

    // Optimization insights
    bool        isRedundant = false;        // Transition to same layout (unnecessary)
    bool        couldBeMerged = false;      // Adjacent barriers on same resource
    std::string optimizationHint;           // Suggestion for improvement
};

// ═══════════════════════════════════════════════════════════════
// Resource Lifetime — when resources are used in the pipeline
// ═══════════════════════════════════════════════════════════════

struct ResourceLifetime {
    std::string     name;
    ResourceKind    kind = ResourceKind::Image;
    bool            imported = false;

    // Usage span (indices into execution order)
    uint32_t        firstUsagePassIndex = 0;
    uint32_t        lastUsagePassIndex = 0;
    std::string     firstUsagePassName;
    std::string     lastUsagePassName;

    // All passes that use this resource (names)
    std::vector<std::string> usedByPasses;

    // Memory analysis
    VkDeviceSize    estimatedSize = 0;      // Bytes (0 if unknown/imported)
    VkFormat        format = VK_FORMAT_UNDEFINED;
    VkExtent2D      extent = {};

    // Optimization insights
    bool            couldBeTransient = false;   // Lives within single queue batch
    bool            isAliasCandidate = false;   // Non-overlapping with others
    std::vector<std::string> aliasableWith;     // Resources this could share memory with
};

// ═══════════════════════════════════════════════════════════════
// Dependency Edge — connection between passes
// ═══════════════════════════════════════════════════════════════

struct DependencyEdge {
    std::string fromPass;           // Producer pass
    std::string toPass;             // Consumer pass
    std::string resourceName;       // Resource connecting them
    std::string accessType;         // "read", "write", "read-write"
    bool        isCrossQueue = false;   // Crosses graphics/compute boundary
};

// ═══════════════════════════════════════════════════════════════
// Queue Batch Analysis — multi-queue scheduling
// ═══════════════════════════════════════════════════════════════

struct QueueBatchInfo {
    std::string             queueType;      // "graphics" or "compute"
    std::vector<std::string> passes;        // Pass names in this batch
    uint32_t                barrierCount = 0;
    bool                    hasSyncBefore = false;  // Waits on other queue
    bool                    hasSyncAfter = false;   // Signals other queue
};

// ═══════════════════════════════════════════════════════════════
// Analysis Statistics — quick metrics
// ═══════════════════════════════════════════════════════════════

struct AnalysisStatistics {
    // Pass counts
    uint32_t totalPasses = 0;
    uint32_t livePasses = 0;
    uint32_t culledPasses = 0;
    uint32_t graphicsPasses = 0;
    uint32_t computePasses = 0;
    uint32_t transferPasses = 0;

    // Resource counts
    uint32_t totalResources = 0;
    uint32_t imageResources = 0;
    uint32_t bufferResources = 0;
    uint32_t importedResources = 0;
    uint32_t transientCandidates = 0;

    // Barrier counts
    uint32_t totalBarriers = 0;
    uint32_t layoutTransitions = 0;
    uint32_t queueTransfers = 0;
    uint32_t redundantBarriers = 0;

    // Memory estimates
    VkDeviceSize estimatedMemoryUsage = 0;      // Total VRAM for owned resources
    VkDeviceSize potentialMemorySavings = 0;    // Savings from aliasing

    // Queue batches
    uint32_t queueBatchCount = 0;
    uint32_t syncPointCount = 0;
};

// ═══════════════════════════════════════════════════════════════
// Full Analysis Result
// ═══════════════════════════════════════════════════════════════

struct AnalysisResult {
    // All findings (sorted by severity: errors first)
    std::vector<AnalysisFinding> findings;

    // Culling analysis
    CullingReport cullingReport;

    // Barrier analysis
    std::vector<BarrierAnalysis> barriers;

    // Resource lifetimes
    std::vector<ResourceLifetime> resourceLifetimes;

    // Dependency graph
    std::vector<DependencyEdge> dependencyEdges;

    // Queue batch analysis
    std::vector<QueueBatchInfo> queueBatches;

    // Quick statistics
    AnalysisStatistics statistics;

    // ── Query helpers ──

    bool hasErrors() const {
        for (const auto& f : findings) {
            if (f.severity == AnalysisSeverity::Error) return true;
        }
        return false;
    }

    bool hasWarnings() const {
        for (const auto& f : findings) {
            if (f.severity == AnalysisSeverity::Warning) return true;
        }
        return false;
    }

    std::vector<AnalysisFinding> getByCategory(const std::string& category) const {
        std::vector<AnalysisFinding> result;
        for (const auto& f : findings) {
            if (f.category == category) result.push_back(f);
        }
        return result;
    }

    std::vector<AnalysisFinding> getBySeverity(AnalysisSeverity severity) const {
        std::vector<AnalysisFinding> result;
        for (const auto& f : findings) {
            if (f.severity == severity) result.push_back(f);
        }
        return result;
    }

    std::vector<AnalysisFinding> getErrors() const { return getBySeverity(AnalysisSeverity::Error); }
    std::vector<AnalysisFinding> getWarnings() const { return getBySeverity(AnalysisSeverity::Warning); }
    std::vector<AnalysisFinding> getSuggestions() const { return getBySeverity(AnalysisSeverity::Suggestion); }
};

// ═══════════════════════════════════════════════════════════════
// Frame Graph Analyzer Class
// 察之明而行之正 — Clear observation leads to correct action
// ═══════════════════════════════════════════════════════════════

// Forward declare CompileResult (defined in FrameGraph.h)
namespace CompilerTypes {
    struct CompileResult;
}

class FrameGraphAnalyzer {
public:
    FrameGraphAnalyzer();
    ~FrameGraphAnalyzer();

    // ═══════════════════════════════════════════════════════════════
    // Pre-Compile Analysis (on builder, before compilation)
    // Useful for early validation and simulation
    // ═══════════════════════════════════════════════════════════════

    // Validate declarations before compilation
    // Checks for: invalid references, cycles, missing bindings
    AnalysisResult analyzeDeclarations(const FrameGraphBuilder& builder);

    // Simulate culling without actual compilation
    // Shows which passes WOULD be culled
    CullingReport simulateCulling(const FrameGraphBuilder& builder);

    // Check for common configuration errors
    std::vector<AnalysisFinding> validateBindings(
        const FrameGraphBuilder& builder,
        const std::unordered_set<std::string>& registeredUBOs = {},
        const std::unordered_set<std::string>& registeredSSBOs = {}
    );

    // ═══════════════════════════════════════════════════════════════
    // Post-Compile Analysis (on compile result)
    // Full analysis with barrier and resource information
    // ═══════════════════════════════════════════════════════════════

    // Full analysis of compiled graph
    template<typename CompileResultT>
    AnalysisResult analyzeCompiled(
        const FrameGraphBuilder& builder,
        const CompileResultT& compiled,
        VkExtent2D referenceExtent = {}
    );

    // Focused analyses (for when you only need specific data)

    template<typename CompileResultT>
    std::vector<BarrierAnalysis> analyzeBarriers(
        const FrameGraphBuilder& builder,
        const CompileResultT& compiled
    );

    template<typename CompileResultT>
    std::vector<ResourceLifetime> analyzeResourceLifetimes(
        const FrameGraphBuilder& builder,
        const CompileResultT& compiled,
        VkExtent2D referenceExtent = {}
    );

    std::vector<DependencyEdge> buildDependencyGraph(
        const FrameGraphBuilder& builder
    );

    // ═══════════════════════════════════════════════════════════════
    // Validation Checks (individual checks)
    // ═══════════════════════════════════════════════════════════════

    // Check for missing bindings in descriptor sets
    std::vector<AnalysisFinding> checkMissingBindings(
        const FrameGraphBuilder& builder,
        const std::unordered_set<std::string>& registeredUBOs = {},
        const std::unordered_set<std::string>& registeredSSBOs = {}
    );

    // Check for unused resources (declared but never used)
    std::vector<AnalysisFinding> checkUnusedResources(
        const FrameGraphBuilder& builder
    );

    // Check for potential write-after-write hazards
    std::vector<AnalysisFinding> checkWriteHazards(
        const FrameGraphBuilder& builder
    );

    // Check for cycles in the dependency graph
    std::vector<AnalysisFinding> checkCycles(
        const FrameGraphBuilder& builder
    );

    // ═══════════════════════════════════════════════════════════════
    // String Conversion Utilities (also used by export)
    // ═══════════════════════════════════════════════════════════════

    static std::string layoutToString(VkImageLayout layout);
    static std::string stageToString(VkPipelineStageFlags stages);
    static std::string accessToString(VkAccessFlags access);
    static std::string formatToString(VkFormat format);
    static std::string passTypeToString(PassType type);
    static std::string queueTypeToString(QueueType type);
    static std::string resourceKindToString(ResourceKind kind);
    static std::string severityToString(AnalysisSeverity severity);

    // Estimate memory size for a resource
    static VkDeviceSize estimateResourceSize(
        const ResourceDeclaration& decl,
        VkExtent2D referenceExtent
    );

private:
    Logger* m_logger = nullptr;

    // Internal helpers
    void analyzeBarriersInternal(
        const std::vector<CompiledPass>& compiledPasses,
        const std::vector<PassDeclaration>& passDecls,
        const std::vector<ResourceDeclaration>& resourceDecls,
        const std::vector<uint32_t>& executionOrder,
        std::vector<BarrierAnalysis>& outBarriers
    );

    void buildCullingReport(
        const std::vector<PassDeclaration>& allPasses,
        const std::vector<uint32_t>& executionOrder,
        CullingReport& outReport
    );

    void analyzeResourceLifetimesInternal(
        const std::vector<ResourceDeclaration>& resourceDecls,
        const std::vector<PassDeclaration>& passDecls,
        const std::vector<uint32_t>& executionOrder,
        VkExtent2D referenceExtent,
        std::vector<ResourceLifetime>& outLifetimes
    );

    void detectAliasOpportunities(
        std::vector<ResourceLifetime>& lifetimes
    );

    void computeStatistics(
        const FrameGraphBuilder& builder,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<BarrierAnalysis>& barriers,
        const std::vector<ResourceLifetime>& lifetimes,
        AnalysisStatistics& outStats
    );
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
