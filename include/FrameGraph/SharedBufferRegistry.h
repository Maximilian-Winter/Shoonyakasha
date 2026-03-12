//
// Shoonyakasha Engine — Shared Buffer Registry
//
// 共有の流れ — The Shared Flow
//
// Cross-graph SSBO sharing: one graph's compute output
// becomes another graph's input, all declared in JSON.
// Non-owning VkBuffer references — the producing graph owns the memory.
//

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace Shoonyakasha {
namespace FrameGraph {

/// Metadata for a shared SSBO/UBO exposed via "target" field in JSON bufferLayouts.
/// The producing RenderGraph owns the actual VulkanBuffer; this holds non-owning handles.
struct SharedBufferEntry {
    std::vector<VkBuffer> buffers;   // Per-frame or single buffer
    VkDeviceSize size = 0;
    uint32_t elementCount = 0;
    uint32_t elementStride = 0;
    uint64_t version = 0;            // Incremented on recompile/recreate
    std::string producerGraph;       // Name of graph that owns the buffer (for debug)
};

/// Metadata for a shared render target image exposed via "target" field in JSON resources.
/// The producing RenderGraph owns the VulkanImage; this holds non-owning handles.
struct SharedImageEntry {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    uint64_t version = 0;
    std::string producerGraph;
};

/// Cross-graph SSBO sharing registry.
///
/// Apps create one instance and inject it into all RenderGraphs that need to share data.
/// When a bufferLayout declares "target": "some.name", the engine registers the SSBO here
/// after creation. Other graphs can reference it via "source": { "type": "buffer_ref", "ref": "some.name" }.
///
/// Usage:
///   auto registry = std::make_unique<SharedBufferRegistry>();
///   graphA->setSharedBufferRegistry(registry.get());
///   graphB->setSharedBufferRegistry(registry.get());
///   graphA->compile(...);  // Creates + registers target SSBOs
///   graphB->compile(...);  // Resolves buffer_ref from registry
///
class SharedBufferRegistry {
public:
    /// Register a buffer under a target name. Overwrites existing entry (version incremented).
    void registerBuffer(const std::string& targetName, const SharedBufferEntry& entry);

    /// Remove a buffer entry (on graph destruction or recompile).
    void unregisterBuffer(const std::string& targetName);

    /// Query a buffer by target name. Returns nullptr if not found.
    const SharedBufferEntry* getBuffer(const std::string& targetName) const;

    /// Check if a target name exists in the registry.
    bool hasBuffer(const std::string& targetName) const;

    /// Get the version of a target entry (0 if not found).
    uint64_t getVersion(const std::string& targetName) const;

    // ── Image sharing (render target data flow) ──

    /// Register a render target image under a target name.
    void registerImage(const std::string& targetName, const SharedImageEntry& entry);

    /// Remove an image entry.
    void unregisterImage(const std::string& targetName);

    /// Query an image by target name. Returns nullptr if not found.
    const SharedImageEntry* getImage(const std::string& targetName) const;

    /// Check if an image target name exists.
    bool hasImage(const std::string& targetName) const;

    /// Clear all entries (for shutdown).
    void clear();

private:
    std::unordered_map<std::string, SharedBufferEntry> m_entries;
    std::unordered_map<std::string, SharedImageEntry> m_imageEntries;
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
