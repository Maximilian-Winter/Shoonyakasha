//
// Shoonyakasha Engine - Frame Graph Analyzer Implementation
//
// 玄武司察  深潛而見真
// The Dark Warrior investigates — diving deep to see truth
//

#include "Vulkan/FrameGraph/FrameGraphAnalyzer.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Core/Logger.h"

#include <algorithm>
#include <queue>
#include <sstream>

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════

FrameGraphAnalyzer::FrameGraphAnalyzer() {
    m_logger = new Logger("framegraph_analyzer.log");
}

FrameGraphAnalyzer::~FrameGraphAnalyzer() {
    delete m_logger;
}

// ═══════════════════════════════════════════════════════════════
// String Conversion Utilities
// ═══════════════════════════════════════════════════════════════

std::string FrameGraphAnalyzer::layoutToString(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:                         return "UNDEFINED";
        case VK_IMAGE_LAYOUT_GENERAL:                           return "GENERAL";
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:          return "COLOR_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:  return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:   return "DEPTH_STENCIL_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:          return "SHADER_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:              return "TRANSFER_SRC_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:              return "TRANSFER_DST_OPTIMAL";
        case VK_IMAGE_LAYOUT_PREINITIALIZED:                    return "PREINITIALIZED";
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:                   return "PRESENT_SRC_KHR";
        default:                                                return "UNKNOWN(" + std::to_string(layout) + ")";
    }
}

std::string FrameGraphAnalyzer::stageToString(VkPipelineStageFlags stages) {
    if (stages == 0) return "NONE";

    std::vector<std::string> parts;
    if (stages & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)             parts.push_back("TOP_OF_PIPE");
    if (stages & VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT)           parts.push_back("DRAW_INDIRECT");
    if (stages & VK_PIPELINE_STAGE_VERTEX_INPUT_BIT)            parts.push_back("VERTEX_INPUT");
    if (stages & VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)           parts.push_back("VERTEX_SHADER");
    if (stages & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)         parts.push_back("FRAGMENT_SHADER");
    if (stages & VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT)    parts.push_back("EARLY_FRAGMENT_TESTS");
    if (stages & VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)     parts.push_back("LATE_FRAGMENT_TESTS");
    if (stages & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) parts.push_back("COLOR_ATTACHMENT_OUTPUT");
    if (stages & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)          parts.push_back("COMPUTE_SHADER");
    if (stages & VK_PIPELINE_STAGE_TRANSFER_BIT)                parts.push_back("TRANSFER");
    if (stages & VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)          parts.push_back("BOTTOM_OF_PIPE");
    if (stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)            parts.push_back("ALL_GRAPHICS");
    if (stages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)            parts.push_back("ALL_COMMANDS");

    if (parts.empty()) return "UNKNOWN(0x" + std::to_string(stages) + ")";

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += " | ";
        result += parts[i];
    }
    return result;
}

std::string FrameGraphAnalyzer::accessToString(VkAccessFlags access) {
    if (access == 0) return "NONE";

    std::vector<std::string> parts;
    if (access & VK_ACCESS_INDIRECT_COMMAND_READ_BIT)           parts.push_back("INDIRECT_COMMAND_READ");
    if (access & VK_ACCESS_INDEX_READ_BIT)                      parts.push_back("INDEX_READ");
    if (access & VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)           parts.push_back("VERTEX_ATTRIBUTE_READ");
    if (access & VK_ACCESS_UNIFORM_READ_BIT)                    parts.push_back("UNIFORM_READ");
    if (access & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT)           parts.push_back("INPUT_ATTACHMENT_READ");
    if (access & VK_ACCESS_SHADER_READ_BIT)                     parts.push_back("SHADER_READ");
    if (access & VK_ACCESS_SHADER_WRITE_BIT)                    parts.push_back("SHADER_WRITE");
    if (access & VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)           parts.push_back("COLOR_ATTACHMENT_READ");
    if (access & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)          parts.push_back("COLOR_ATTACHMENT_WRITE");
    if (access & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)   parts.push_back("DEPTH_STENCIL_ATTACHMENT_READ");
    if (access & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)  parts.push_back("DEPTH_STENCIL_ATTACHMENT_WRITE");
    if (access & VK_ACCESS_TRANSFER_READ_BIT)                   parts.push_back("TRANSFER_READ");
    if (access & VK_ACCESS_TRANSFER_WRITE_BIT)                  parts.push_back("TRANSFER_WRITE");
    if (access & VK_ACCESS_MEMORY_READ_BIT)                     parts.push_back("MEMORY_READ");
    if (access & VK_ACCESS_MEMORY_WRITE_BIT)                    parts.push_back("MEMORY_WRITE");

    if (parts.empty()) return "UNKNOWN(0x" + std::to_string(access) + ")";

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += " | ";
        result += parts[i];
    }
    return result;
}

std::string FrameGraphAnalyzer::formatToString(VkFormat format) {
    switch (format) {
        case VK_FORMAT_UNDEFINED:           return "UNDEFINED";
        case VK_FORMAT_R8_UNORM:            return "R8_UNORM";
        case VK_FORMAT_R8G8_UNORM:          return "R8G8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM:      return "R8G8B8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SRGB:       return "R8G8B8A8_SRGB";
        case VK_FORMAT_B8G8R8A8_UNORM:      return "B8G8R8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_SRGB:       return "B8G8R8A8_SRGB";
        case VK_FORMAT_R16_SFLOAT:          return "R16_SFLOAT";
        case VK_FORMAT_R16G16_SFLOAT:       return "R16G16_SFLOAT";
        case VK_FORMAT_R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
        case VK_FORMAT_R32_SFLOAT:          return "R32_SFLOAT";
        case VK_FORMAT_R32G32_SFLOAT:       return "R32G32_SFLOAT";
        case VK_FORMAT_R32G32B32A32_SFLOAT: return "R32G32B32A32_SFLOAT";
        case VK_FORMAT_D16_UNORM:           return "D16_UNORM";
        case VK_FORMAT_D32_SFLOAT:          return "D32_SFLOAT";
        case VK_FORMAT_D24_UNORM_S8_UINT:   return "D24_UNORM_S8_UINT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT:  return "D32_SFLOAT_S8_UINT";
        default:                            return "FORMAT(" + std::to_string(format) + ")";
    }
}

std::string FrameGraphAnalyzer::passTypeToString(PassType type) {
    switch (type) {
        case PassType::Graphics:    return "graphics";
        case PassType::Compute:     return "compute";
        case PassType::Transfer:    return "transfer";
        default:                    return "unknown";
    }
}

std::string FrameGraphAnalyzer::queueTypeToString(QueueType type) {
    switch (type) {
        case QueueType::Graphics:   return "graphics";
        case QueueType::Compute:    return "compute";
        default:                    return "unknown";
    }
}

std::string FrameGraphAnalyzer::resourceKindToString(ResourceKind kind) {
    switch (kind) {
        case ResourceKind::Image:   return "image";
        case ResourceKind::Buffer:  return "buffer";
        default:                    return "unknown";
    }
}

std::string FrameGraphAnalyzer::severityToString(AnalysisSeverity severity) {
    switch (severity) {
        case AnalysisSeverity::Info:        return "INFO";
        case AnalysisSeverity::Suggestion:  return "SUGGESTION";
        case AnalysisSeverity::Warning:     return "WARNING";
        case AnalysisSeverity::Error:       return "ERROR";
        default:                            return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════
// Resource Size Estimation
// ═══════════════════════════════════════════════════════════════

VkDeviceSize FrameGraphAnalyzer::estimateResourceSize(
    const ResourceDeclaration& decl,
    VkExtent2D referenceExtent)
{
    if (decl.imported) return 0;  // Can't estimate imported resources

    if (decl.kind == ResourceKind::Buffer) {
        return decl.bufferDesc.size;
    }

    // Image resource
    uint32_t width = decl.imageDesc.width;
    uint32_t height = decl.imageDesc.height;

    // Apply scale factors if dimensions are 0
    if (width == 0) {
        width = static_cast<uint32_t>(referenceExtent.width * decl.imageDesc.widthScale);
    }
    if (height == 0) {
        height = static_cast<uint32_t>(referenceExtent.height * decl.imageDesc.heightScale);
    }

    // Estimate bytes per pixel based on format
    uint32_t bytesPerPixel = 4;  // Default assumption
    switch (decl.imageDesc.format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
            bytesPerPixel = 1;
            break;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_D16_UNORM:
            bytesPerPixel = 2;
            break;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
            bytesPerPixel = 4;
            break;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            bytesPerPixel = 5;  // 4 + 1 (packed)
            break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32G32_SFLOAT:
            bytesPerPixel = 8;
            break;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            bytesPerPixel = 16;
            break;
        default:
            bytesPerPixel = 4;
            break;
    }

    VkDeviceSize baseSize = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;

    // Account for samples
    baseSize *= decl.imageDesc.samples;

    // Account for array layers
    baseSize *= decl.imageDesc.arrayLayers;

    // Account for mip levels (roughly 1.33x for full mip chain)
    if (decl.imageDesc.mipLevels > 1) {
        baseSize = static_cast<VkDeviceSize>(baseSize * 1.33);
    }

    return baseSize;
}

// ═══════════════════════════════════════════════════════════════
// Dependency Graph Building
// ═══════════════════════════════════════════════════════════════

std::vector<DependencyEdge> FrameGraphAnalyzer::buildDependencyGraph(
    const FrameGraphBuilder& builder)
{
    std::vector<DependencyEdge> edges;
    const auto& passes = builder.getPassDeclarations();
    const auto& resources = builder.getResourceDeclarations();

    // Track which pass writes each resource
    std::unordered_map<uint32_t, std::string> resourceWriter;

    for (const auto& pass : passes) {
        // Record outputs
        for (const auto& output : pass.outputs) {
            if (output.handle.valid()) {
                resourceWriter[output.handle.index] = pass.name;
            }
        }
    }

    // Build edges from producer to consumer
    for (const auto& pass : passes) {
        for (const auto& input : pass.inputs) {
            if (!input.handle.valid()) continue;

            auto writerIt = resourceWriter.find(input.handle.index);
            if (writerIt != resourceWriter.end() && writerIt->second != pass.name) {
                DependencyEdge edge;
                edge.fromPass = writerIt->second;
                edge.toPass = pass.name;

                if (input.handle.index < resources.size()) {
                    edge.resourceName = resources[input.handle.index].name;
                }

                // Determine access type
                bool isWrite = (input.usage == ResourceUsage::ShaderReadWrite ||
                                input.usage == ResourceUsage::StorageImageWrite);
                edge.accessType = isWrite ? "read-write" : "read";

                // Check if cross-queue
                for (const auto& producer : passes) {
                    if (producer.name == edge.fromPass) {
                        edge.isCrossQueue = (producer.queueType != pass.queueType);
                        break;
                    }
                }

                edges.push_back(edge);
            }
        }
    }

    return edges;
}

// ═══════════════════════════════════════════════════════════════
// Culling Report Building
// ═══════════════════════════════════════════════════════════════

void FrameGraphAnalyzer::buildCullingReport(
    const std::vector<PassDeclaration>& allPasses,
    const std::vector<uint32_t>& executionOrder,
    CullingReport& outReport)
{
    // Build set of live pass indices
    std::unordered_set<uint32_t> liveIndices(executionOrder.begin(), executionOrder.end());

    // Categorize passes
    for (uint32_t i = 0; i < allPasses.size(); ++i) {
        const auto& pass = allPasses[i];
        if (liveIndices.count(i)) {
            outReport.livePasses.push_back(pass.name);
        } else {
            outReport.culledPasses.push_back(pass.name);

            // Determine WHY this pass was culled
            std::string reason;

            // Check if outputs are consumed
            bool hasConsumedOutput = false;
            bool hasOutputsOnlyImported = true;
            for (const auto& output : pass.outputs) {
                if (!output.handle.valid()) continue;

                // Check if any live pass reads this output
                for (uint32_t liveIdx : executionOrder) {
                    const auto& livePass = allPasses[liveIdx];
                    for (const auto& input : livePass.inputs) {
                        if (input.handle.index == output.handle.index) {
                            hasConsumedOutput = true;
                            break;
                        }
                    }
                    if (hasConsumedOutput) break;
                }

                // Check if output writes to imported resource with Present usage
                if (output.usage == ResourceUsage::Present) {
                    hasOutputsOnlyImported = false;
                }
            }

            if (pass.outputs.empty()) {
                if (pass.hasSideEffects) {
                    reason = "Has side effects but outputs not tracked (check hasSideEffects flag)";
                } else {
                    reason = "No outputs declared";
                }
            } else if (!hasConsumedOutput) {
                reason = "No outputs consumed by any live pass";
            } else {
                reason = "All output consumers were also culled (cascade)";
            }

            outReport.cullReasons[pass.name] = reason;
        }
    }

    // Build cascade effects
    // For each culled pass, find other culled passes that depended on it
    for (const auto& culledName : outReport.culledPasses) {
        std::vector<std::string> affected;

        // Find the culled pass
        const PassDeclaration* culledPass = nullptr;
        for (const auto& p : allPasses) {
            if (p.name == culledName) {
                culledPass = &p;
                break;
            }
        }
        if (!culledPass) continue;

        // Get resource handles this pass outputs
        std::unordered_set<uint32_t> outputHandles;
        for (const auto& out : culledPass->outputs) {
            if (out.handle.valid()) {
                outputHandles.insert(out.handle.index);
            }
        }

        // Find other culled passes that read these outputs
        for (const auto& otherCulled : outReport.culledPasses) {
            if (otherCulled == culledName) continue;

            for (const auto& p : allPasses) {
                if (p.name == otherCulled) {
                    for (const auto& in : p.inputs) {
                        if (in.handle.valid() && outputHandles.count(in.handle.index)) {
                            affected.push_back(otherCulled);
                            break;
                        }
                    }
                    break;
                }
            }
        }

        if (!affected.empty()) {
            outReport.cascadeEffects[culledName] = affected;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Pre-Compile Analysis
// ═══════════════════════════════════════════════════════════════

AnalysisResult FrameGraphAnalyzer::analyzeDeclarations(const FrameGraphBuilder& builder) {
    AnalysisResult result;

    // Check for cycles
    auto cycleFindings = checkCycles(builder);
    result.findings.insert(result.findings.end(), cycleFindings.begin(), cycleFindings.end());

    // Check for unused resources
    auto unusedFindings = checkUnusedResources(builder);
    result.findings.insert(result.findings.end(), unusedFindings.begin(), unusedFindings.end());

    // Check for write hazards
    auto hazardFindings = checkWriteHazards(builder);
    result.findings.insert(result.findings.end(), hazardFindings.begin(), hazardFindings.end());

    // Build dependency graph
    result.dependencyEdges = buildDependencyGraph(builder);

    // Simulate culling
    result.cullingReport = simulateCulling(builder);

    // Basic statistics
    result.statistics.totalPasses = static_cast<uint32_t>(builder.getPassDeclarations().size());
    result.statistics.totalResources = static_cast<uint32_t>(builder.getResourceDeclarations().size());

    for (const auto& pass : builder.getPassDeclarations()) {
        switch (pass.type) {
            case PassType::Graphics: result.statistics.graphicsPasses++; break;
            case PassType::Compute:  result.statistics.computePasses++; break;
            case PassType::Transfer: result.statistics.transferPasses++; break;
        }
    }

    for (const auto& res : builder.getResourceDeclarations()) {
        if (res.imported) result.statistics.importedResources++;
        if (res.kind == ResourceKind::Image) result.statistics.imageResources++;
        else result.statistics.bufferResources++;
    }

    return result;
}

CullingReport FrameGraphAnalyzer::simulateCulling(const FrameGraphBuilder& builder) {
    CullingReport report;

    const auto& passes = builder.getPassDeclarations();
    const auto& resources = builder.getResourceDeclarations();

    if (passes.empty()) return report;

    // Run simplified topological sort + dead pass detection
    // This mirrors what the compiler does

    // Build adjacency list for dependencies
    std::unordered_map<uint32_t, uint32_t> resourceWriter;
    std::vector<std::vector<uint32_t>> adj(passes.size());
    std::vector<uint32_t> inDegree(passes.size(), 0);

    // Map pass names to indices
    std::unordered_map<std::string, uint32_t> passIndex;
    for (uint32_t i = 0; i < passes.size(); ++i) {
        passIndex[passes[i].name] = i;
    }

    // Track which pass writes each resource
    for (uint32_t i = 0; i < passes.size(); ++i) {
        for (const auto& output : passes[i].outputs) {
            if (output.handle.valid()) {
                resourceWriter[output.handle.index] = i;
            }
        }
    }

    // Build edges
    for (uint32_t i = 0; i < passes.size(); ++i) {
        for (const auto& input : passes[i].inputs) {
            if (!input.handle.valid()) continue;
            auto it = resourceWriter.find(input.handle.index);
            if (it != resourceWriter.end() && it->second != i) {
                adj[it->second].push_back(i);
                inDegree[i]++;
            }
        }
    }

    // Topological sort (Kahn's algorithm)
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < passes.size(); ++i) {
        if (inDegree[i] == 0) q.push(i);
    }

    std::vector<uint32_t> topoOrder;
    while (!q.empty()) {
        uint32_t u = q.front();
        q.pop();
        topoOrder.push_back(u);

        for (uint32_t v : adj[u]) {
            if (--inDegree[v] == 0) {
                q.push(v);
            }
        }
    }

    // Dead pass culling: backward reachability from Present outputs and side-effect passes
    std::unordered_set<uint32_t> live;

    // Find "root" passes (write to swapchain/Present or have side effects)
    for (uint32_t i = 0; i < passes.size(); ++i) {
        const auto& pass = passes[i];

        if (pass.hasSideEffects) {
            live.insert(i);
            continue;
        }

        for (const auto& output : pass.outputs) {
            if (output.usage == ResourceUsage::Present) {
                live.insert(i);
                break;
            }
            // Also consider imported resources as potentially live
            if (output.handle.valid() && output.handle.index < resources.size()) {
                if (resources[output.handle.index].imported) {
                    live.insert(i);
                    break;
                }
            }
        }
    }

    // Backward propagation: if a pass is live, its dependencies are also live
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (live.count(i)) continue;

            // Check if any live pass depends on this pass
            for (uint32_t dep : adj[i]) {
                if (live.count(dep)) {
                    live.insert(i);
                    changed = true;
                    break;
                }
            }
        }
    }

    // Build execution order (only live passes, in topo order)
    std::vector<uint32_t> executionOrder;
    for (uint32_t idx : topoOrder) {
        if (live.count(idx)) {
            executionOrder.push_back(idx);
        }
    }

    // Build the report
    buildCullingReport(passes, executionOrder, report);

    return report;
}

// ═══════════════════════════════════════════════════════════════
// Validation Checks
// ═══════════════════════════════════════════════════════════════

std::vector<AnalysisFinding> FrameGraphAnalyzer::validateBindings(
    const FrameGraphBuilder& builder,
    const std::unordered_set<std::string>& registeredUBOs,
    const std::unordered_set<std::string>& registeredSSBOs)
{
    std::vector<AnalysisFinding> findings;
    return checkMissingBindings(builder, registeredUBOs, registeredSSBOs);
}

std::vector<AnalysisFinding> FrameGraphAnalyzer::checkMissingBindings(
    const FrameGraphBuilder& builder,
    const std::unordered_set<std::string>& registeredUBOs,
    const std::unordered_set<std::string>& registeredSSBOs)
{
    std::vector<AnalysisFinding> findings;

    // Build set of available resources
    std::unordered_set<std::string> availableResources;
    for (const auto& res : builder.getResourceDeclarations()) {
        availableResources.insert(res.name);
    }

    // Build set of available samplers
    std::unordered_set<std::string> availableSamplers;
    for (const auto& sampler : builder.getSamplers()) {
        availableSamplers.insert(sampler.name);
    }

    // Check each descriptor set layout for auto-bind references
    for (const auto& layout : builder.getDescriptorSetLayouts()) {
        for (const auto& binding : layout.bindings) {
            // Check autoBindResource
            if (!binding.autoBindResource.empty()) {
                if (availableResources.find(binding.autoBindResource) == availableResources.end()) {
                    AnalysisFinding f;
                    f.severity = AnalysisSeverity::Warning;
                    f.category = "binding";
                    f.message = "Descriptor set '" + layout.name + "' binding '" + binding.name +
                                "' references resource '" + binding.autoBindResource + "' which is not declared";
                    f.suggestion = "Add resource declaration or fix the autoBindResource name";
                    findings.push_back(f);
                }
            }

            // Check autoBindSampler
            if (!binding.autoBindSampler.empty()) {
                if (availableSamplers.find(binding.autoBindSampler) == availableSamplers.end()) {
                    AnalysisFinding f;
                    f.severity = AnalysisSeverity::Warning;
                    f.category = "binding";
                    f.message = "Descriptor set '" + layout.name + "' binding '" + binding.name +
                                "' references sampler '" + binding.autoBindSampler + "' which is not declared";
                    f.suggestion = "Add sampler declaration in JSON or fix the autoBindSampler name";
                    findings.push_back(f);
                }
            }

            // Check autoBindBuffer
            if (!binding.autoBindBuffer.empty()) {
                bool found = registeredUBOs.count(binding.autoBindBuffer) ||
                             registeredSSBOs.count(binding.autoBindBuffer);
                if (!found) {
                    AnalysisFinding f;
                    f.severity = AnalysisSeverity::Warning;
                    f.category = "binding";
                    f.message = "Descriptor set '" + layout.name + "' binding '" + binding.name +
                                "' references buffer '" + binding.autoBindBuffer +
                                "' which may not be registered";
                    f.suggestion = "Call registerUniformBuffer() or registerStorageBuffer() before compile()";
                    findings.push_back(f);
                }
            }
        }
    }

    return findings;
}

std::vector<AnalysisFinding> FrameGraphAnalyzer::checkUnusedResources(
    const FrameGraphBuilder& builder)
{
    std::vector<AnalysisFinding> findings;

    const auto& resources = builder.getResourceDeclarations();
    const auto& passes = builder.getPassDeclarations();

    // Track which resources are actually used
    std::unordered_set<uint32_t> usedResources;

    for (const auto& pass : passes) {
        for (const auto& input : pass.inputs) {
            if (input.handle.valid()) {
                usedResources.insert(input.handle.index);
            }
        }
        for (const auto& output : pass.outputs) {
            if (output.handle.valid()) {
                usedResources.insert(output.handle.index);
            }
        }
    }

    // Report unused resources
    for (uint32_t i = 0; i < resources.size(); ++i) {
        if (usedResources.find(i) == usedResources.end()) {
            const auto& res = resources[i];
            AnalysisFinding f;
            f.severity = AnalysisSeverity::Suggestion;
            f.category = "resource";
            f.resourceName = res.name;
            f.message = "Resource '" + res.name + "' is declared but never used by any pass";
            f.suggestion = "Remove the resource declaration or add it to a pass's inputs/outputs";
            findings.push_back(f);
        }
    }

    return findings;
}

std::vector<AnalysisFinding> FrameGraphAnalyzer::checkWriteHazards(
    const FrameGraphBuilder& builder)
{
    std::vector<AnalysisFinding> findings;

    const auto& passes = builder.getPassDeclarations();
    const auto& resources = builder.getResourceDeclarations();

    // Track writers per resource
    std::unordered_map<uint32_t, std::vector<std::string>> resourceWriters;

    for (const auto& pass : passes) {
        for (const auto& output : pass.outputs) {
            if (output.handle.valid()) {
                resourceWriters[output.handle.index].push_back(pass.name);
            }
        }
    }

    // Report resources with multiple writers (potential WAW hazard)
    for (const auto& [resIdx, writers] : resourceWriters) {
        if (writers.size() > 1 && resIdx < resources.size()) {
            const auto& res = resources[resIdx];

            // This is a potential issue if there's no explicit ordering
            AnalysisFinding f;
            f.severity = AnalysisSeverity::Warning;
            f.category = "hazard";
            f.resourceName = res.name;

            std::string writerList;
            for (size_t i = 0; i < writers.size(); ++i) {
                if (i > 0) writerList += ", ";
                writerList += "'" + writers[i] + "'";
            }

            f.message = "Resource '" + res.name + "' is written by multiple passes: " + writerList;
            f.suggestion = "Ensure passes have proper dependency ordering or use separate resources";
            findings.push_back(f);
        }
    }

    return findings;
}

std::vector<AnalysisFinding> FrameGraphAnalyzer::checkCycles(
    const FrameGraphBuilder& builder)
{
    std::vector<AnalysisFinding> findings;

    const auto& passes = builder.getPassDeclarations();
    if (passes.empty()) return findings;

    // Build adjacency list
    std::unordered_map<uint32_t, uint32_t> resourceWriter;
    std::vector<std::vector<uint32_t>> adj(passes.size());
    std::vector<uint32_t> inDegree(passes.size(), 0);

    for (uint32_t i = 0; i < passes.size(); ++i) {
        for (const auto& output : passes[i].outputs) {
            if (output.handle.valid()) {
                resourceWriter[output.handle.index] = i;
            }
        }
    }

    for (uint32_t i = 0; i < passes.size(); ++i) {
        for (const auto& input : passes[i].inputs) {
            if (!input.handle.valid()) continue;
            auto it = resourceWriter.find(input.handle.index);
            if (it != resourceWriter.end() && it->second != i) {
                adj[it->second].push_back(i);
                inDegree[i]++;
            }
        }
    }

    // Kahn's algorithm to detect cycle
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < passes.size(); ++i) {
        if (inDegree[i] == 0) q.push(i);
    }

    uint32_t processed = 0;
    while (!q.empty()) {
        uint32_t u = q.front();
        q.pop();
        processed++;

        for (uint32_t v : adj[u]) {
            if (--inDegree[v] == 0) {
                q.push(v);
            }
        }
    }

    if (processed < passes.size()) {
        // Cycle detected - find participants
        std::vector<std::string> cycleParticipants;
        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (inDegree[i] > 0) {
                cycleParticipants.push_back(passes[i].name);
            }
        }

        AnalysisFinding f;
        f.severity = AnalysisSeverity::Error;
        f.category = "dependency";
        f.message = "Cycle detected in pass dependencies. Involved passes: ";
        for (size_t i = 0; i < cycleParticipants.size(); ++i) {
            if (i > 0) f.message += ", ";
            f.message += "'" + cycleParticipants[i] + "'";
        }
        f.suggestion = "Remove circular dependencies between passes";
        findings.push_back(f);
    }

    return findings;
}

// ═══════════════════════════════════════════════════════════════
// Post-Compile Analysis (template implementations)
// ═══════════════════════════════════════════════════════════════

// These are implemented as explicit template instantiations for the actual CompileResult type
// The implementations use the internal helpers defined below

void FrameGraphAnalyzer::analyzeBarriersInternal(
    const std::vector<CompiledPass>& compiledPasses,
    const std::vector<PassDeclaration>& passDecls,
    const std::vector<ResourceDeclaration>& resourceDecls,
    const std::vector<uint32_t>& executionOrder,
    std::vector<BarrierAnalysis>& outBarriers)
{
    for (uint32_t passIdx : executionOrder) {
        if (passIdx >= compiledPasses.size()) continue;

        const auto& compiled = compiledPasses[passIdx];
        const auto& passDecl = passDecls[compiled.declIndex];

        // Analyze pre-barriers
        for (const auto& barrier : compiled.preBarriers) {
            BarrierAnalysis analysis;
            analysis.passName = passDecl.name;

            if (barrier.resource.valid() && barrier.resource.index < resourceDecls.size()) {
                analysis.resourceName = resourceDecls[barrier.resource.index].name;
            }

            analysis.oldLayout = barrier.oldLayout;
            analysis.newLayout = barrier.newLayout;
            analysis.oldLayoutStr = layoutToString(barrier.oldLayout);
            analysis.newLayoutStr = layoutToString(barrier.newLayout);

            analysis.srcStage = barrier.srcStage;
            analysis.dstStage = barrier.dstStage;
            analysis.srcStageStr = stageToString(barrier.srcStage);
            analysis.dstStageStr = stageToString(barrier.dstStage);

            analysis.srcAccess = barrier.srcAccess;
            analysis.dstAccess = barrier.dstAccess;

            // Check for queue ownership transfer
            if (barrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED) {
                analysis.isQueueOwnershipTransfer = true;
                // Note: We don't have queue family names, just indices
                analysis.srcQueueType = "queue_" + std::to_string(barrier.srcQueueFamilyIndex);
                analysis.dstQueueType = "queue_" + std::to_string(barrier.dstQueueFamilyIndex);
            }

            // Check for redundant barrier
            if (barrier.oldLayout == barrier.newLayout && !analysis.isQueueOwnershipTransfer) {
                analysis.isRedundant = true;
                analysis.optimizationHint = "Transition to same layout - could be removed";
            }

            outBarriers.push_back(analysis);
        }

        // Analyze acquire barriers (queue ownership transfers)
        for (const auto& barrier : compiled.acquireBarriers) {
            BarrierAnalysis analysis;
            analysis.passName = passDecl.name;

            if (barrier.resource.valid() && barrier.resource.index < resourceDecls.size()) {
                analysis.resourceName = resourceDecls[barrier.resource.index].name;
            }

            analysis.oldLayout = barrier.oldLayout;
            analysis.newLayout = barrier.newLayout;
            analysis.oldLayoutStr = layoutToString(barrier.oldLayout);
            analysis.newLayoutStr = layoutToString(barrier.newLayout);

            analysis.srcStage = barrier.srcStage;
            analysis.dstStage = barrier.dstStage;
            analysis.srcStageStr = stageToString(barrier.srcStage);
            analysis.dstStageStr = stageToString(barrier.dstStage);

            analysis.srcAccess = barrier.srcAccess;
            analysis.dstAccess = barrier.dstAccess;

            analysis.isQueueOwnershipTransfer = true;
            analysis.srcQueueType = "queue_" + std::to_string(barrier.srcQueueFamilyIndex);
            analysis.dstQueueType = "queue_" + std::to_string(barrier.dstQueueFamilyIndex);

            outBarriers.push_back(analysis);
        }
    }
}

void FrameGraphAnalyzer::analyzeResourceLifetimesInternal(
    const std::vector<ResourceDeclaration>& resourceDecls,
    const std::vector<PassDeclaration>& passDecls,
    const std::vector<uint32_t>& executionOrder,
    VkExtent2D referenceExtent,
    std::vector<ResourceLifetime>& outLifetimes)
{
    // Initialize lifetimes
    outLifetimes.resize(resourceDecls.size());

    for (uint32_t i = 0; i < resourceDecls.size(); ++i) {
        const auto& decl = resourceDecls[i];
        outLifetimes[i].name = decl.name;
        outLifetimes[i].kind = decl.kind;
        outLifetimes[i].imported = decl.imported;
        outLifetimes[i].format = decl.imageDesc.format;
        outLifetimes[i].firstUsagePassIndex = UINT32_MAX;
        outLifetimes[i].lastUsagePassIndex = 0;

        // Calculate extent
        if (decl.kind == ResourceKind::Image && !decl.imported) {
            uint32_t w = decl.imageDesc.width;
            uint32_t h = decl.imageDesc.height;
            if (w == 0) w = static_cast<uint32_t>(referenceExtent.width * decl.imageDesc.widthScale);
            if (h == 0) h = static_cast<uint32_t>(referenceExtent.height * decl.imageDesc.heightScale);
            outLifetimes[i].extent = {w, h};
        }

        outLifetimes[i].estimatedSize = estimateResourceSize(decl, referenceExtent);
    }

    // Track usage across execution order
    for (uint32_t orderIdx = 0; orderIdx < executionOrder.size(); ++orderIdx) {
        uint32_t passIdx = executionOrder[orderIdx];
        if (passIdx >= passDecls.size()) continue;

        const auto& pass = passDecls[passIdx];

        auto processResource = [&](const ResourceAccess& access) {
            if (!access.handle.valid() || access.handle.index >= resourceDecls.size()) return;

            uint32_t resIdx = access.handle.index;
            auto& lifetime = outLifetimes[resIdx];

            if (orderIdx < lifetime.firstUsagePassIndex) {
                lifetime.firstUsagePassIndex = orderIdx;
                lifetime.firstUsagePassName = pass.name;
            }
            if (orderIdx > lifetime.lastUsagePassIndex || lifetime.lastUsagePassIndex == 0) {
                lifetime.lastUsagePassIndex = orderIdx;
                lifetime.lastUsagePassName = pass.name;
            }

            // Add to used-by list (avoid duplicates)
            if (std::find(lifetime.usedByPasses.begin(), lifetime.usedByPasses.end(), pass.name)
                == lifetime.usedByPasses.end()) {
                lifetime.usedByPasses.push_back(pass.name);
            }
        };

        for (const auto& input : pass.inputs) processResource(input);
        for (const auto& output : pass.outputs) processResource(output);
    }

    // Mark resources that were never used
    for (auto& lifetime : outLifetimes) {
        if (lifetime.firstUsagePassIndex == UINT32_MAX) {
            lifetime.firstUsagePassIndex = 0;
            lifetime.firstUsagePassName = "(unused)";
            lifetime.lastUsagePassName = "(unused)";
        }
    }
}

void FrameGraphAnalyzer::detectAliasOpportunities(std::vector<ResourceLifetime>& lifetimes) {
    // Find non-overlapping resources with similar sizes that could share memory
    for (size_t i = 0; i < lifetimes.size(); ++i) {
        if (lifetimes[i].imported) continue;
        if (lifetimes[i].estimatedSize == 0) continue;

        for (size_t j = i + 1; j < lifetimes.size(); ++j) {
            if (lifetimes[j].imported) continue;
            if (lifetimes[j].estimatedSize == 0) continue;

            // Check if lifetimes don't overlap
            bool nonOverlapping =
                (lifetimes[i].lastUsagePassIndex < lifetimes[j].firstUsagePassIndex) ||
                (lifetimes[j].lastUsagePassIndex < lifetimes[i].firstUsagePassIndex);

            if (nonOverlapping) {
                // Check if sizes are compatible (within 2x of each other)
                VkDeviceSize larger = std::max(lifetimes[i].estimatedSize, lifetimes[j].estimatedSize);
                VkDeviceSize smaller = std::min(lifetimes[i].estimatedSize, lifetimes[j].estimatedSize);

                if (smaller * 2 >= larger) {
                    lifetimes[i].isAliasCandidate = true;
                    lifetimes[j].isAliasCandidate = true;
                    lifetimes[i].aliasableWith.push_back(lifetimes[j].name);
                    lifetimes[j].aliasableWith.push_back(lifetimes[i].name);
                }
            }
        }
    }

    // Mark resources that could be transient (short lifetime, single batch)
    for (auto& lifetime : lifetimes) {
        if (lifetime.imported) continue;
        if (lifetime.firstUsagePassIndex == lifetime.lastUsagePassIndex) {
            lifetime.couldBeTransient = true;
        }
    }
}

void FrameGraphAnalyzer::computeStatistics(
    const FrameGraphBuilder& builder,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<BarrierAnalysis>& barriers,
    const std::vector<ResourceLifetime>& lifetimes,
    AnalysisStatistics& outStats)
{
    const auto& passes = builder.getPassDeclarations();
    const auto& resources = builder.getResourceDeclarations();

    // Pass statistics
    outStats.totalPasses = static_cast<uint32_t>(passes.size());
    outStats.livePasses = static_cast<uint32_t>(executionOrder.size());
    outStats.culledPasses = outStats.totalPasses - outStats.livePasses;

    for (const auto& pass : passes) {
        switch (pass.type) {
            case PassType::Graphics: outStats.graphicsPasses++; break;
            case PassType::Compute:  outStats.computePasses++; break;
            case PassType::Transfer: outStats.transferPasses++; break;
        }
    }

    // Resource statistics
    outStats.totalResources = static_cast<uint32_t>(resources.size());
    for (const auto& res : resources) {
        if (res.imported) outStats.importedResources++;
        if (res.kind == ResourceKind::Image) outStats.imageResources++;
        else outStats.bufferResources++;
    }

    for (const auto& lifetime : lifetimes) {
        if (lifetime.couldBeTransient) outStats.transientCandidates++;
        outStats.estimatedMemoryUsage += lifetime.estimatedSize;
    }

    // Barrier statistics
    outStats.totalBarriers = static_cast<uint32_t>(barriers.size());
    for (const auto& barrier : barriers) {
        if (barrier.oldLayout != barrier.newLayout) outStats.layoutTransitions++;
        if (barrier.isQueueOwnershipTransfer) outStats.queueTransfers++;
        if (barrier.isRedundant) outStats.redundantBarriers++;
    }

    // Calculate potential savings from aliasing
    for (const auto& lifetime : lifetimes) {
        if (lifetime.isAliasCandidate && !lifetime.aliasableWith.empty()) {
            // Roughly, we could save half the memory by aliasing pairs
            outStats.potentialMemorySavings += lifetime.estimatedSize / 2;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Template Instantiation for analyzeCompiled
// ═══════════════════════════════════════════════════════════════

template<>
AnalysisResult FrameGraphAnalyzer::analyzeCompiled<FrameGraphCompiler::CompileResult>(
    const FrameGraphBuilder& builder,
    const FrameGraphCompiler::CompileResult& compiled,
    VkExtent2D referenceExtent)
{
    AnalysisResult result;

    if (!compiled.valid) {
        result.findings.push_back(AnalysisFinding::error(
            "compilation", "Graph compilation failed: " + compiled.errorMessage));
        return result;
    }

    const auto& passes = builder.getPassDeclarations();
    const auto& resources = builder.getResourceDeclarations();

    // Build culling report
    buildCullingReport(passes, compiled.executionOrder, result.cullingReport);

    // Add warning for each culled pass
    for (const auto& culled : result.cullingReport.culledPasses) {
        auto it = result.cullingReport.cullReasons.find(culled);
        std::string reason = (it != result.cullingReport.cullReasons.end()) ? it->second : "Unknown reason";

        AnalysisFinding f;
        f.severity = AnalysisSeverity::Warning;
        f.category = "culling";
        f.passName = culled;
        f.message = "Pass '" + culled + "' was culled: " + reason;
        f.suggestion = "Ensure the pass outputs are consumed by a live pass, or set hasSideEffects=true";
        result.findings.push_back(f);
    }

    // Analyze barriers
    analyzeBarriersInternal(compiled.compiledPasses, passes, resources,
                           compiled.executionOrder, result.barriers);

    // Analyze resource lifetimes
    analyzeResourceLifetimesInternal(resources, passes, compiled.executionOrder,
                                     referenceExtent, result.resourceLifetimes);

    // Detect aliasing opportunities
    detectAliasOpportunities(result.resourceLifetimes);

    // Build dependency graph
    result.dependencyEdges = buildDependencyGraph(builder);

    // Compute statistics
    computeStatistics(builder, compiled.executionOrder, result.barriers,
                      result.resourceLifetimes, result.statistics);

    // Sort findings by severity (errors first)
    std::sort(result.findings.begin(), result.findings.end(),
              [](const AnalysisFinding& a, const AnalysisFinding& b) {
                  return static_cast<int>(a.severity) > static_cast<int>(b.severity);
              });

    return result;
}

template<>
std::vector<BarrierAnalysis> FrameGraphAnalyzer::analyzeBarriers<FrameGraphCompiler::CompileResult>(
    const FrameGraphBuilder& builder,
    const FrameGraphCompiler::CompileResult& compiled)
{
    std::vector<BarrierAnalysis> barriers;
    if (!compiled.valid) return barriers;

    analyzeBarriersInternal(compiled.compiledPasses, builder.getPassDeclarations(),
                           builder.getResourceDeclarations(), compiled.executionOrder, barriers);
    return barriers;
}

template<>
std::vector<ResourceLifetime> FrameGraphAnalyzer::analyzeResourceLifetimes<FrameGraphCompiler::CompileResult>(
    const FrameGraphBuilder& builder,
    const FrameGraphCompiler::CompileResult& compiled,
    VkExtent2D referenceExtent)
{
    std::vector<ResourceLifetime> lifetimes;
    if (!compiled.valid) return lifetimes;

    analyzeResourceLifetimesInternal(builder.getResourceDeclarations(), builder.getPassDeclarations(),
                                     compiled.executionOrder, referenceExtent, lifetimes);
    detectAliasOpportunities(lifetimes);
    return lifetimes;
}

} // namespace FrameGraph
} // namespace Shoonyakasha
