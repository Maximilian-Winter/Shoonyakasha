//
// PixelReadback.h - One texel of a graph image copied back to the CPU
//
// RenderGraph::requestPixelReadback copies one texel of an image after a
// named pass into a host-visible buffer of the frame in flight recording it.
// The value is read when that frame in flight records again, after the
// application has waited for its fence. This header holds the parts that need
// no GPU: the formats read, the barriers around the copy, and the queue of
// requests and results.
//

#pragma once

#include "FrameGraphResource.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Shoonyakasha {
namespace FrameGraph {

struct CompiledPass;

/// A texel copied back: its first component, where it was taken and in which
/// frame (RenderGraph's count of recorded frames).
struct PixelReadbackResult {
    bool valid = false;
    float value = 0.0f;
    VkOffset2D pixel{0, 0};
    uint64_t frame = 0;
};

/// Bytes the copy of one texel of `format` writes: of the depth aspect for
/// depth formats. 0 for formats that cannot be read back: D32_SFLOAT,
/// D32_SFLOAT_S8_UINT, D16_UNORM, X8_D24_UNORM_PACK32, D24_UNORM_S8_UINT,
/// R32_SFLOAT, R16_SFLOAT and R8_UNORM can.
uint32_t pixelReadbackTexelSize(VkFormat format);

/// The aspect copied: depth for depth formats, colour otherwise.
VkImageAspectFlags pixelReadbackAspect(VkFormat format);

/// The first component of a texel of `format` as copied to a buffer.
float decodeReadbackTexel(VkFormat format, const void* texel);

/// The layout `pass` leaves `resource` in: its post-barrier's new layout, or
/// else the layout it renders the resource in as an attachment. Nothing when
/// the pass does neither.
std::optional<VkImageLayout> layoutAfterPass(const CompiledPass& pass, ResourceHandle resource);

/// The barriers around copying from `image`, which is in `layout`: to
/// TRANSFER_SRC_OPTIMAL after any earlier access, and back to `layout` before
/// any later one. Mip 0, layer 0 of `aspect`.
std::array<VkImageMemoryBarrier2, 2> pixelReadbackBarriers(VkImage image, VkImageLayout layout,
                                                           VkImageAspectFlags aspect);

/// Requests and results. At most one request waits to be recorded, and each
/// frame in flight holds at most one copy until it records again.
class PixelReadbackQueue {
public:
    struct Request {
        std::string resource;
        std::string afterPass;
        VkOffset2D pixel{0, 0};
    };

    /// Drops copies in flight; results already read stay.
    void setFramesInFlight(uint32_t count);

    /// Replaces a request not yet recorded.
    void request(Request request) { m_next = std::move(request); }
    const std::optional<Request>& next() const { return m_next; }
    void dropNext() { m_next.reset(); }

    /// The next request was recorded into frame in flight `frameIndex`, in
    /// frame `frame`, from an image of `format`. Takes it off the queue.
    void recorded(uint32_t frameIndex, VkFormat format, uint64_t frame);

    /// Does frame in flight `frameIndex` hold a copy not yet read?
    bool awaiting(uint32_t frameIndex) const;

    /// Reads the copy of frame in flight `frameIndex` from `texel`, the
    /// buffer it was copied to, once that frame's fence has been waited on.
    void complete(uint32_t frameIndex, const void* texel);

    /// The newest copy read.
    const PixelReadbackResult& latest() const { return m_latest; }

private:
    struct Slot {
        bool awaiting = false;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkOffset2D pixel{0, 0};
        uint64_t frame = 0;
    };

    std::optional<Request> m_next;
    std::vector<Slot> m_slots;
    PixelReadbackResult m_latest;
};

} // namespace FrameGraph
} // namespace Shoonyakasha
