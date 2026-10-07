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
#include <functional>
#include <optional>
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

// ═══════════════════════════════════════════════════════════════
// Buffers
// ═══════════════════════════════════════════════════════════════

/// One pass's access to one tracked buffer.
struct BufferAccess {
    uint32_t             buffer = 0;   // index into BufferAccessTable::buffers
    VkPipelineStageFlags stages = 0;
    VkAccessFlags        access = 0;
};

/// What a pass's shaders do with one storage buffer binding, found by
/// reflecting them: the stages that declare it, and whether any of them may
/// write it (a block not declared readonly).
struct ShaderBufferUse {
    VkPipelineStageFlags stages = 0;
    bool                 writes = false;
};

/// Asks about pass `pass` (declaration index), descriptor set `set` (position
/// in its "descriptorSets") and `binding`. std::nullopt means the shaders
/// could not be inspected; the binding's JSON stages are then taken as
/// reading and writing it.
using ShaderBufferQuery =
    std::function<std::optional<ShaderBufferUse>(uint32_t pass, uint32_t set, uint32_t binding)>;

struct BufferAccessTable {
    /// Tracked buffers: graph buffer resources ("resource:<name>"), the
    /// "autoBindBuffer" names of uniform and storage buffer bindings, and
    /// last, "*", every buffer the graph cannot name.
    std::vector<std::string> buffers;
    /// Indexed by pass declaration index; at most one entry per buffer.
    std::vector<std::vector<BufferAccess>> passes;
};

/// The buffers each pass reads and writes on the GPU. Graph buffer resources
/// come from inputs and outputs; other buffers from the pass's descriptor
/// sets, narrowed by `query`. Uniform buffers count as reads; the host writes
/// them before the frame is submitted, which needs no barrier.
///
/// A pass that records through code the graph cannot see is taken to touch
/// every buffer: one with an execute callback, an execution type of "none",
/// "manual" or "scene_geometry", or an entity geometry type (its renderer
/// binds vertex and index buffers). Graphics passes of that kind are taken to
/// read, compute and transfer passes to read and write. A uniform or storage
/// binding without "autoBindBuffer" likewise reads any buffer, and writes any
/// when it is a storage buffer its shaders may write.
BufferAccessTable describeBufferAccesses(
    const std::vector<PassDeclaration>& passes,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<DescriptorSetLayoutDesc>& descriptorLayouts,
    const ShaderBufferQuery& query = {});

/// A global memory barrier. Empty (no stages) when none is needed.
struct MemoryBarrierInfo {
    VkPipelineStageFlags srcStage  = 0;
    VkPipelineStageFlags dstStage  = 0;
    VkAccessFlags        srcAccess = 0;
    VkAccessFlags        dstAccess = 0;

    bool empty() const { return dstStage == 0; }
};

/// For each pass (by declaration index), the one memory barrier to place
/// before it so it sees earlier writes to the buffers it reads, and does not
/// overwrite a buffer earlier passes are still using. Reads after reads need
/// none. Planned like image barriers, across the frame boundary: a frame's
/// first access waits for the previous frame's last.
std::vector<MemoryBarrierInfo> planBufferBarriers(
    const std::vector<uint32_t>& executionOrder,
    const BufferAccessTable& table);

// ═══════════════════════════════════════════════════════════════
// Attachment load and store operations
// ═══════════════════════════════════════════════════════════════

struct AttachmentOps {
    /// Nothing earlier this frame wrote the attachment's subresources, so a
    /// load would only fetch undefined contents: LOAD_OP_DONT_CARE.
    bool discardLoad  = false;
    /// Nothing later reads what the pass rendered: STORE_OP_DONT_CARE.
    bool discardStore = false;
};

/// For each pass (by declaration index), one entry per output, in output
/// order; only attachment outputs of passes in `executionOrder` are set.
///
/// A load is discarded for a colour or depth write without a clear that is
/// the first access to its subresources in the frame, of an image that is
/// not persistent. A store is discarded for a colour or depth write that does
/// not present, of an image that is not imported, persistent, read back,
/// saved or shared as a target, when no later pass reads those subresources:
/// as an input, by loading or blending onto them, through any descriptor set
/// that binds the image, or through code the graph cannot see (an execute
/// callback, or execution type "none", "manual" or "scene_geometry").
std::vector<std::vector<AttachmentOps>> planAttachmentOps(
    const std::vector<PassDeclaration>& passes,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<DescriptorSetLayoutDesc>& descriptorLayouts,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<ScheduledResource>& resources);

} // namespace FrameGraph
} // namespace Shoonyakasha
