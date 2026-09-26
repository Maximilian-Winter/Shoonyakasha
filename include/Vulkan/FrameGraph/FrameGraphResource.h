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
// Image shape: view types and subresource ranges
// ═══════════════════════════════════════════════════════════════

/// How an image resource is seen by shaders that sample the whole of it,
/// JSON image "viewType".
enum class ImageViewKind {
    Auto,       // "auto": 2d for one layer, 2d_array for more
    Tex2D,      // "2d"
    Tex2DArray, // "2d_array"
    Cube,       // "cube": exactly 6 layers, square
    CubeArray   // "cube_array": a multiple of 6 layers, square
};

/// A block of mip levels and array layers of an image, as a pass access or a
/// descriptor binding names it. Counts of kAll run to the end of the image,
/// so the default range is the whole image.
struct SubresourceRange {
    static constexpr uint32_t kAll = std::numeric_limits<uint32_t>::max();

    uint32_t baseMip   = 0;
    uint32_t mipCount  = kAll;
    uint32_t baseLayer = 0;
    uint32_t layerCount = kAll;

    bool isWholeImage() const {
        return baseMip == 0 && mipCount == kAll && baseLayer == 0 && layerCount == kAll;
    }
    bool operator==(const SubresourceRange&) const = default;
};

/// Mip and layer counts of a created image; what a SubresourceRange is
/// resolved against.
struct ImageShape {
    uint32_t mipLevels   = 1;
    uint32_t arrayLayers = 1;

    /// Replace kAll counts with the remainder of the image. The result may
    /// still reach past the image; check it with contains().
    SubresourceRange resolve(const SubresourceRange& r) const {
        SubresourceRange out = r;
        if (out.mipCount == SubresourceRange::kAll)
            out.mipCount = out.baseMip < mipLevels ? mipLevels - out.baseMip : 0;
        if (out.layerCount == SubresourceRange::kAll)
            out.layerCount = out.baseLayer < arrayLayers ? arrayLayers - out.baseLayer : 0;
        return out;
    }

    /// Whether a resolved range is non-empty and lies inside the image.
    bool contains(const SubresourceRange& resolved) const {
        return resolved.mipCount > 0 && resolved.layerCount > 0 &&
               resolved.baseMip + resolved.mipCount <= mipLevels &&
               resolved.baseLayer + resolved.layerCount <= arrayLayers;
    }
};

/// Length of a full mip chain for an image of this size.
inline uint32_t fullMipChainLength(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    uint32_t size = width > height ? width : height;
    while (size > 1) { size >>= 1; ++levels; }
    return levels;
}

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

    // 0 means a full mip chain for the resolved size (JSON "mipLevels": "full").
    uint32_t mipLevels   = 1;
    uint32_t arrayLayers = 1;
    ImageViewKind viewType = ImageViewKind::Auto;

    // For future transient aliasing (Phase 5)
    bool transient = false;

    // Contents carry over from one frame to the next (JSON "persistent"):
    // a frame's first access does not discard them. Cleared to zero when the
    // graph is compiled. For state a pass accumulates, such as an adapted
    // exposure.
    bool persistent = false;
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
