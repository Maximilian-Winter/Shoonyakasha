//
// Shoonyakasha Engine - Frame Graph JSON Serialization
//
// 名不正則言不順  言不順則事不成
// If names are not correct, language is not in accordance
// If language is not in accordance, affairs cannot be completed
//

#pragma once

#include "FrameGraphResource.h"
#include "FrameGraphPass.h"

#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>

namespace Shoonyakasha {
namespace FrameGraph {

// Forward declaration
class FrameGraphBuilder;

// ═══════════════════════════════════════════════════════════════
// String ↔ Enum Conversion Tables
// ═══════════════════════════════════════════════════════════════

namespace JsonUtils {

    // VkFormat string conversion
    VkFormat        stringToFormat(const std::string& str);
    std::string     formatToString(VkFormat format);

    // ResourceUsage string conversion
    ResourceUsage   stringToResourceUsage(const std::string& str);
    std::string     resourceUsageToString(ResourceUsage usage);

    // PassType string conversion
    PassType        stringToPassType(const std::string& str);
    std::string     passTypeToString(PassType type);

    // ResourceKind string conversion
    ResourceKind    stringToResourceKind(const std::string& str);
    std::string     resourceKindToString(ResourceKind kind);

    // VkShaderStageFlags string conversion
    VkShaderStageFlags stringToShaderStage(const std::string& str);
    VkShaderStageFlags stringsToShaderStages(const std::vector<std::string>& strs);

    // VkDescriptorType string conversion
    VkDescriptorType   stringToDescriptorType(const std::string& str);
    std::string        descriptorTypeToString(VkDescriptorType type);

    // QueueType string conversion
    QueueType          stringToQueueType(const std::string& str);
    std::string        queueTypeToString(QueueType type);

    // Sampler string conversions
    VkFilter              stringToFilter(const std::string& str);
    VkSamplerMipmapMode   stringToMipmapMode(const std::string& str);
    VkSamplerAddressMode  stringToAddressMode(const std::string& str);
    VkBorderColor         stringToBorderColor(const std::string& str);
    VkCompareOp           stringToCompareOp(const std::string& str);

} // namespace JsonUtils

// ═══════════════════════════════════════════════════════════════
// JSON Loading — populate a FrameGraphBuilder from JSON
// ═══════════════════════════════════════════════════════════════

// Load a complete render graph from a JSON object
void loadGraphFromJson(FrameGraphBuilder& builder, const nlohmann::json& json);

// Load from a JSON file path
void loadGraphFromFile(FrameGraphBuilder& builder, const std::string& filePath);

// ═══════════════════════════════════════════════════════════════
// JSON Saving — serialize a FrameGraphBuilder to JSON
// ═══════════════════════════════════════════════════════════════

// Serialize a graph to JSON
nlohmann::json saveGraphToJson(const FrameGraphBuilder& builder);

// Save to a file
void saveGraphToFile(const FrameGraphBuilder& builder, const std::string& filePath);

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
