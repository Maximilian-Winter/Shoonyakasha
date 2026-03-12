//
// Shoonyakasha Engine - Frame Graph Resource Declarations
//
// 約束為位  殘差為時
// Constraints establish position; residuals reveal timing
//

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>
#include <limits>

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Resource Handle — lightweight typed reference into the graph
// ═══════════════════════════════════════════════════════════════

struct ResourceHandle {
    uint32_t index = std::numeric_limits<uint32_t>::max();

    bool valid() const { return index != std::numeric_limits<uint32_t>::max(); }
    auto operator<=>(const ResourceHandle&) const = default;
    bool operator==(const ResourceHandle&) const = default;
};

// ═══════════════════════════════════════════════════════════════
// Image Resource Descriptor — what the graph wants
// ═══════════════════════════════════════════════════════════════

struct ImageDesc {
    uint32_t width  = 0;            // 0 means "match reference extent"
    uint32_t height = 0;            // 0 means "match reference extent"
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags additionalUsage = 0;  // Graph will OR in required usage

    // Scale relative to reference extent (used when width/height = 0)
    float widthScale  = 1.0f;
    float heightScale = 1.0f;

    uint32_t mipLevels   = 1;
    uint32_t arrayLayers = 1;

    // For future transient aliasing (Phase 5)
    bool transient = false;
};

// ═══════════════════════════════════════════════════════════════
// Buffer Resource Descriptor
// ═══════════════════════════════════════════════════════════════

struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    bool persistentlyMapped = false;
};

// ═══════════════════════════════════════════════════════════════
// Resource Declaration — a named resource in the graph
// ═══════════════════════════════════════════════════════════════

enum class ResourceKind {
    Image,
    Buffer
};

struct ResourceDeclaration {
    std::string     name;
    ResourceKind    kind = ResourceKind::Image;
    ImageDesc       imageDesc{};
    BufferDesc      bufferDesc{};
    bool            imported = false;   // true for swapchain, external UBOs, etc.

    // ── Target/readback/save for render target data flow ──
    // Mirrors SSBO target/readback/save functionality from bufferLayouts.
    // Enables cross-graph image sharing, periodic GPU→CPU readback, and disk saves.
    std::string target;  // Register in shared resource registry (empty = private)

    struct ResourceReadbackPolicy {
        bool enabled = false;
        std::string frequency = "manual";  // "manual", "per_frame", "every_n_frames", "once"
        uint32_t n = 1;
        bool callbackEnabled = false;
        uint32_t ringDepth = 0;  // 0 = auto (maxFramesInFlight)
    } readbackPolicy;

    struct ResourceSavePolicy {
        bool enabled = false;
        std::string path;
        std::string trigger = "manual";  // "manual", "every_n_frames", "on_readback"
        uint32_t n = 1;
        bool autoCreateDirectories = true;
    } savePolicy;
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
