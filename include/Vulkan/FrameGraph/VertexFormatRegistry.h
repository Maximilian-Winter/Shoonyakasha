//
// VertexFormatRegistry.h - Declarative vertex input format definitions
//
// Parses JSON "vertexFormats" section and converts named formats into
// Vulkan vertex input binding/attribute descriptions for pipeline creation.
//
// 頂點之構 — The structure of vertices arises from declaration
//

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Shoonyakasha {
namespace FrameGraph {

// ============================================================================
// VertexAttributeDeclaration - A single vertex attribute from JSON
// ============================================================================

struct VertexAttributeDeclaration {
    std::string name;       // e.g., "position", "normal", "jointIndices"
    std::string type;       // e.g., "vec3", "vec4", "uvec4", "float"
    uint32_t    location;   // Shader location index

    // Computed at registration time
    VkFormat    vkFormat = VK_FORMAT_UNDEFINED;
    uint32_t    size = 0;   // Size in bytes
    uint32_t    offset = 0; // Offset within vertex (auto-computed)
};

// ============================================================================
// VertexFormatDeclaration - A complete named vertex format
// ============================================================================

struct VertexFormatDeclaration {
    std::string name;       // e.g., "standard", "skinned"
    std::vector<VertexAttributeDeclaration> attributes;
    uint32_t stride = 0;    // Total vertex size in bytes (auto-computed)
};

// ============================================================================
// VertexFormatRegistry - Stores and resolves named vertex formats
// ============================================================================

class VertexFormatRegistry {
public:
    // Register a vertex format (call during JSON parsing)
    void registerFormat(const VertexFormatDeclaration& format);

    // Check if a format is registered
    bool hasFormat(const std::string& name) const;

    // Get the format declaration (nullptr if not found)
    const VertexFormatDeclaration* getFormat(const std::string& name) const;

    // Get Vulkan vertex input descriptions for a named format
    // Returns false if format not found
    bool getVertexInputDescriptions(
        const std::string& name,
        VkVertexInputBindingDescription& outBinding,
        std::vector<VkVertexInputAttributeDescription>& outAttributes
    ) const;

    // Clear all registered formats
    void clear();

    // Get all format names
    std::vector<std::string> getFormatNames() const;

    // ─── Type Conversion Utilities ──────────────────────────
    static VkFormat typeStringToVkFormat(const std::string& type);
    static uint32_t typeStringToSize(const std::string& type);

private:
    std::unordered_map<std::string, VertexFormatDeclaration> m_formats;
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
