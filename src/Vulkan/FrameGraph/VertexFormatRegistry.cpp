//
// VertexFormatRegistry.cpp - Declarative vertex input format resolution
//

#include "Vulkan/FrameGraph/VertexFormatRegistry.h"

namespace Shoonyakasha {
namespace FrameGraph {

// ============================================================================
// Type Conversion Tables
// ============================================================================

VkFormat VertexFormatRegistry::typeStringToVkFormat(const std::string& type) {
    if (type == "float")  return VK_FORMAT_R32_SFLOAT;
    if (type == "vec2")   return VK_FORMAT_R32G32_SFLOAT;
    if (type == "vec3")   return VK_FORMAT_R32G32B32_SFLOAT;
    if (type == "vec4")   return VK_FORMAT_R32G32B32A32_SFLOAT;
    if (type == "int")    return VK_FORMAT_R32_SINT;
    if (type == "ivec2")  return VK_FORMAT_R32G32_SINT;
    if (type == "ivec3")  return VK_FORMAT_R32G32B32_SINT;
    if (type == "ivec4")  return VK_FORMAT_R32G32B32A32_SINT;
    if (type == "uint")   return VK_FORMAT_R32_UINT;
    if (type == "uvec2")  return VK_FORMAT_R32G32_UINT;
    if (type == "uvec3")  return VK_FORMAT_R32G32B32_UINT;
    if (type == "uvec4")  return VK_FORMAT_R32G32B32A32_UINT;
    // 8-bit normalized (useful for colors)
    if (type == "unorm4") return VK_FORMAT_R8G8B8A8_UNORM;
    return VK_FORMAT_UNDEFINED;
}

uint32_t VertexFormatRegistry::typeStringToSize(const std::string& type) {
    if (type == "float")  return 4;
    if (type == "vec2")   return 8;
    if (type == "vec3")   return 12;
    if (type == "vec4")   return 16;
    if (type == "int")    return 4;
    if (type == "ivec2")  return 8;
    if (type == "ivec3")  return 12;
    if (type == "ivec4")  return 16;
    if (type == "uint")   return 4;
    if (type == "uvec2")  return 8;
    if (type == "uvec3")  return 12;
    if (type == "uvec4")  return 16;
    if (type == "unorm4") return 4;
    return 0;
}

// ============================================================================
// Registration
// ============================================================================

void VertexFormatRegistry::registerFormat(const VertexFormatDeclaration& format) {
    // Make a copy and compute offsets/sizes
    VertexFormatDeclaration resolved = format;
    uint32_t currentOffset = 0;

    for (auto& attr : resolved.attributes) {
        attr.vkFormat = typeStringToVkFormat(attr.type);
        attr.size = typeStringToSize(attr.type);
        attr.offset = currentOffset;
        currentOffset += attr.size;
    }

    resolved.stride = currentOffset;
    m_formats[resolved.name] = std::move(resolved);
}

bool VertexFormatRegistry::hasFormat(const std::string& name) const {
    return m_formats.find(name) != m_formats.end();
}

const VertexFormatDeclaration* VertexFormatRegistry::getFormat(const std::string& name) const {
    auto it = m_formats.find(name);
    return it != m_formats.end() ? &it->second : nullptr;
}

bool VertexFormatRegistry::getVertexInputDescriptions(
    const std::string& name,
    VkVertexInputBindingDescription& outBinding,
    std::vector<VkVertexInputAttributeDescription>& outAttributes
) const {
    auto it = m_formats.find(name);
    if (it == m_formats.end()) {
        return false;
    }

    const auto& format = it->second;

    // Single binding at binding 0, per-vertex rate
    outBinding = {};
    outBinding.binding = 0;
    outBinding.stride = format.stride;
    outBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // One attribute per declared field
    outAttributes.clear();
    outAttributes.reserve(format.attributes.size());

    for (const auto& attr : format.attributes) {
        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.binding = 0;
        vkAttr.location = attr.location;
        vkAttr.format = attr.vkFormat;
        vkAttr.offset = attr.offset;
        outAttributes.push_back(vkAttr);
    }

    return true;
}

void VertexFormatRegistry::clear() {
    m_formats.clear();
}

std::vector<std::string> VertexFormatRegistry::getFormatNames() const {
    std::vector<std::string> names;
    names.reserve(m_formats.size());
    for (const auto& [name, _] : m_formats) {
        names.push_back(name);
    }
    return names;
}

} // namespace FrameGraph
} // namespace Shoonyakasha
