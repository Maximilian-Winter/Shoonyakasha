//
// Shoonyakasha Engine - Frame Graph Export
//
// 青龍司生  生發而有序
// The Azure Dragon governs growth — making the invisible visible
//
// Export and visualization functions for frame graphs.
// DOT/Graphviz, JSON, and human-readable text reports.
//

#pragma once

#include "FrameGraphAnalyzer.h"

#include <string>
#include <functional>

namespace Shoonyakasha {
namespace FrameGraph {

// Forward declarations
class FrameGraphBuilder;

// ═══════════════════════════════════════════════════════════════
// Export Options
// 見其形而知其道 — See the form to know the Way
// ═══════════════════════════════════════════════════════════════

struct ExportOptions {
    // ── DOT Visualization Style ──

    // Resource node display:
    //   true  = Full graph with both pass and resource nodes
    //   false = Pass-centric graph (cleaner, edges labeled with resource names)
    bool showResourceNodes = true;

    // Show culled passes (grayed out with dashed border)
    bool showCulledPasses = true;

    // Show barrier information on edges
    bool showBarriers = true;

    // Coloring schemes
    bool colorByQueue = true;      // Color passes by queue type (graphics=blue, compute=green)
    bool colorByPassType = false;  // Alternative: color by graphics/compute/transfer type

    // Group passes into queue batch clusters
    bool clusterByBatch = true;

    // Graph direction: "TB" (top-bottom), "LR" (left-right), "BT", "RL"
    std::string graphRankDir = "TB";

    // Node shapes
    std::string passNodeShape = "box";
    std::string resourceNodeShape = "oval";

    // ── JSON Options ──

    bool includeStatistics = true;
    bool includeBarrierDetails = true;
    bool includeLifetimeDetails = true;
    bool prettyPrint = true;

    // ── Text Report Options ──

    bool verboseMode = false;          // Extended explanations
    bool showOptimizationHints = true; // Include suggestions for improvement
    bool showExecutionOrder = true;    // List passes in execution order
    bool showResourceDetails = true;   // Include resource format/size info

    // ── Filtering ──

    // Optional: only include these passes (empty = all)
    std::vector<std::string> filterPasses;

    // Optional: only include these resources (empty = all)
    std::vector<std::string> filterResources;
};

// ═══════════════════════════════════════════════════════════════
// DOT/Graphviz Export
// 圖之以形  明之以道
// Diagram the form, illuminate the Way
// ═══════════════════════════════════════════════════════════════

// Generate DOT format string for Graphviz visualization
std::string exportToDot(
    const FrameGraphBuilder& builder,
    const AnalysisResult& analysis,
    const ExportOptions& options = {}
);

// Export directly to a .dot file
bool exportToDotFile(
    const std::string& filePath,
    const FrameGraphBuilder& builder,
    const AnalysisResult& analysis,
    const ExportOptions& options = {}
);

// ═══════════════════════════════════════════════════════════════
// JSON Export
// 數據之序  萬物之理
// The order of data, the principle of all things
// ═══════════════════════════════════════════════════════════════

// Generate JSON representation of the analysis
std::string exportToJson(
    const FrameGraphBuilder& builder,
    const AnalysisResult& analysis,
    const ExportOptions& options = {}
);

// Export directly to a .json file
bool exportToJsonFile(
    const std::string& filePath,
    const FrameGraphBuilder& builder,
    const AnalysisResult& analysis,
    const ExportOptions& options = {}
);

// ═══════════════════════════════════════════════════════════════
// Human-Readable Reports
// 言之明而行之正 — Clear speech leads to correct action
// ═══════════════════════════════════════════════════════════════

// Generate a full markdown report
std::string generateMarkdownReport(
    const FrameGraphBuilder& builder,
    const AnalysisResult& analysis,
    const ExportOptions& options = {}
);

// Generate a concise text summary (for console output)
std::string generateTextSummary(
    const AnalysisResult& analysis,
    const ExportOptions& options = {}
);

// Generate a detailed culling report
std::string generateCullingReport(
    const CullingReport& culling,
    const ExportOptions& options = {}
);

// Generate a barrier transition report
std::string generateBarrierReport(
    const std::vector<BarrierAnalysis>& barriers,
    const ExportOptions& options = {}
);

// Generate a resource lifetime report
std::string generateResourceLifetimeReport(
    const std::vector<ResourceLifetime>& lifetimes,
    const ExportOptions& options = {}
);

// ═══════════════════════════════════════════════════════════════
// Helper: Pass-through to Logger
// ═══════════════════════════════════════════════════════════════

// Log the analysis summary (convenience wrapper)
void logAnalysisSummary(
    const AnalysisResult& analysis,
    std::function<void(const std::string&)> logFunc
);

// Log findings with appropriate severity levels
void logFindings(
    const std::vector<AnalysisFinding>& findings,
    std::function<void(AnalysisSeverity, const std::string&)> logFunc
);

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
