//
// Shoonyakasha Engine - Frame Graph Scheduling
//
// Which passes depend on which, and which barriers go between them, worked out
// per mip level and array layer. Nothing here touches a device, so all of it
// is exercised by the unit tests.
//

#pragma once

#include "FrameGraphPass.h"
#include "FrameGraphResource.h"

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Barriers
// ═══════════════════════════════════════════════════════════════

struct BarrierInfo {
    ResourceHandle          resource;
    // Every member is defaulted, so a member added later without an
    // assignment is never indeterminate.
    VkImageLayout           oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout           newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags    srcStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags    dstStage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkAccessFlags           srcAccess = 0;
    VkAccessFlags           dstAccess = 0;

    /// Mips and layers the barrier covers, with the image's aspect mask.
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    // Queue ownership transfer (VK_QUEUE_FAMILY_IGNORED = no transfer)
    uint32_t                srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    uint32_t                dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
};

// ═══════════════════════════════════════════════════════════════
// Resources as the scheduler sees them
// ═══════════════════════════════════════════════════════════════

struct ScheduledResource {
    bool isImage = false;
    /// Created mip and layer counts. Buffers and imported images are 1 x 1.
    ImageShape shape;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    /// An imported image with one view per swapchain image. When no pass
    /// declares "present" on it, its last writer presents it.
    bool presentFallback = false;
    /// Contents carry over between frames: a frame's first barrier keeps the
    /// layout the previous frame left rather than starting from UNDEFINED.
    bool persistent = false;
};

/// Build the scheduler's view of every declared resource from the
/// declarations alone. mipLevels 0 ("full") needs the resolved size, so it is
/// taken from `extents` where given (one entry per resource, {0,0} if unknown);
/// with no size it counts as one level.
std::vector<ScheduledResource> describeResources(
    const std::vector<ResourceDeclaration>& resources,
    const std::vector<VkExtent2D>& extents = {});

// ═══════════════════════════════════════════════════════════════
// Usage tables
// ═══════════════════════════════════════════════════════════════

VkImageLayout        usageToLayout(ResourceUsage usage);
VkPipelineStageFlags usageToStageMask(ResourceUsage usage, PassType passType);
VkAccessFlags        usageToAccessMask(ResourceUsage usage);

/// Whether an access depends on what earlier passes left in its
/// subresources: every input, and outputs that blend, test against depth
/// read-only, read-modify-write a storage image, or load an attachment
/// instead of clearing it.
bool readsPreviousContents(const ResourceAccess& access, bool isOutput);

/// Whether an output is rendered to as a colour or depth attachment.
bool isAttachmentUsage(ResourceUsage usage);

// ═══════════════════════════════════════════════════════════════
// Validation
// ═══════════════════════════════════════════════════════════════

/// Check every access and auto-bound descriptor range against its image:
/// ranges must lie inside the image, attachments must name one mip level, and
/// one pass may not access a subresource twice in different layouts.
/// Returns false and appends one line per problem to `outError`.
bool validateSubresources(const std::vector<PassDeclaration>& passes,
                          const std::vector<ResourceDeclaration>& declarations,
                          const std::vector<ScheduledResource>& resources,
                          const std::vector<DescriptorSetLayoutDesc>& descriptorLayouts,
                          std::string& outError);

// ═══════════════════════════════════════════════════════════════
// Dependencies
// ═══════════════════════════════════════════════════════════════

/// For each pass (by declaration index), the earlier-declared passes whose
/// writes it can see: for every subresource it reads, the most recent earlier
/// writer of that subresource. Two passes writing different layers of one
/// image are therefore independent, and a reader of the whole image depends
/// on both. Each list is sorted and free of duplicates; every edge points
/// from a lower to a higher declaration index.
std::vector<std::vector<uint32_t>> passDependencies(
    const std::vector<PassDeclaration>& passes,
    const std::vector<ScheduledResource>& resources);

/// A topological order of all passes over `dependencies`. Ready passes are
/// taken in declaration order, so independent passes keep the order they
/// were declared in. Returns false with the passes involved if there is a
/// cycle.
bool sortPasses(const std::vector<PassDeclaration>& passes,
                const std::vector<std::vector<uint32_t>>& dependencies,
                std::vector<uint32_t>& outOrder,
                std::string& outError);

/// Passes that must run: those that present, write an imported resource, or
/// have side effects, and everything they depend on. Returned sorted.
std::vector<uint32_t> livePasses(
    const std::vector<PassDeclaration>& passes,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<std::vector<uint32_t>>& dependencies);

// ═══════════════════════════════════════════════════════════════
// Views
// ═══════════════════════════════════════════════════════════════

/// View type for sampling `resolved` of an image declared as `kind`. The
/// whole layer range keeps the declared type; part of it becomes 2d for one
/// layer, cube or cube_array for whole cubes of a cube image, and 2d_array
/// otherwise. A 2d_array image stays 2d_array, so shaders see one type.
VkImageViewType sampledViewType(ImageViewKind kind, const ImageShape& shape,
                                const SubresourceRange& resolved);

/// View type for rendering into `resolved`: 2d for one layer, 2d_array for
/// layered rendering.
VkImageViewType attachmentViewType(const SubresourceRange& resolved);

// ═══════════════════════════════════════════════════════════════
// Barrier planning
// ═══════════════════════════════════════════════════════════════

struct BarrierPlan {
    /// Indexed by pass declaration index.
    std::vector<std::vector<BarrierInfo>> preBarriers;
    std::vector<std::vector<BarrierInfo>> postBarriers;

    /// Layout each subresource is left in at the end of the frame, indexed by
    /// resource, then mip * arrayLayers + layer. Empty for buffers.
    std::vector<std::vector<VkImageLayout>> finalLayouts;

    /// Presentable images whose presenting pass had to be inferred.
    std::vector<std::string> warnings;
};

/// Barriers for executing `executionOrder`. A barrier is placed before an
/// access whenever the layout changes or either side writes; consecutive
/// reads in one layout share the earlier barrier. Barriers for neighbouring
/// layers and mips with the same prior state are merged into one range.
/// An output that presents gets a post-barrier to PRESENT_SRC_KHR.
BarrierPlan planBarriers(
    const std::vector<PassDeclaration>& passes,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<ScheduledResource>& resources);

} // namespace FrameGraph
} // namespace Shoonyakasha
