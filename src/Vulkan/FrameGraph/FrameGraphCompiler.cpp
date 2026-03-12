//
// Shoonyakasha Engine - Frame Graph Compiler
//
// 玄武司察  深潛而見真
// The Dark Warrior governs investigation — diving deep to see truth
//

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanRenderPass.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanMemoryAllocator.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Core/Logger.h"

#include <queue>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cassert>

namespace Shoonyakasha {
namespace FrameGraph {

FrameGraphCompiler::~FrameGraphCompiler() {
    delete m_logger;
}

// ═══════════════════════════════════════════════════════════════
// Usage → Vulkan mapping helpers
// ═══════════════════════════════════════════════════════════════

VkImageLayout FrameGraphCompiler::usageToLayout(ResourceUsage usage) {
    switch (usage) {
        case ResourceUsage::ColorAttachmentWrite:   return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ResourceUsage::ColorAttachmentBlend:   return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // Same layout, blending is pipeline state
        case ResourceUsage::DepthStencilWrite:      return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ResourceUsage::DepthStencilReadOnly:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case ResourceUsage::ShaderReadOnly:         return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ResourceUsage::ShaderReadWrite:        return VK_IMAGE_LAYOUT_GENERAL;
        case ResourceUsage::StorageImageWrite:      return VK_IMAGE_LAYOUT_GENERAL;
        case ResourceUsage::InputAttachment:        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ResourceUsage::TransferSrc:            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case ResourceUsage::TransferDst:            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case ResourceUsage::Present:                return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkPipelineStageFlags FrameGraphCompiler::usageToStageMask(ResourceUsage usage, PassType passType) {
    switch (usage) {
        case ResourceUsage::ColorAttachmentWrite:
        case ResourceUsage::ColorAttachmentBlend:  // Blending uses same stage as color write
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case ResourceUsage::DepthStencilWrite:
        case ResourceUsage::DepthStencilReadOnly:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case ResourceUsage::ShaderReadOnly:
        case ResourceUsage::ShaderReadWrite:
        case ResourceUsage::StorageImageWrite:
        case ResourceUsage::InputAttachment:
            if (passType == PassType::Compute) return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case ResourceUsage::TransferSrc:
        case ResourceUsage::TransferDst:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case ResourceUsage::Present:
            return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}

VkAccessFlags FrameGraphCompiler::usageToAccessMask(ResourceUsage usage) {
    switch (usage) {
        case ResourceUsage::ColorAttachmentWrite:
            return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case ResourceUsage::ColorAttachmentBlend:
            // Blending requires READ (to fetch dst color) + WRITE (to output blended result)
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case ResourceUsage::DepthStencilWrite:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case ResourceUsage::DepthStencilReadOnly:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case ResourceUsage::ShaderReadOnly:
        case ResourceUsage::InputAttachment:
            return VK_ACCESS_SHADER_READ_BIT;
        case ResourceUsage::ShaderReadWrite:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        case ResourceUsage::StorageImageWrite:
            return VK_ACCESS_SHADER_WRITE_BIT;
        case ResourceUsage::TransferSrc:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case ResourceUsage::TransferDst:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case ResourceUsage::Present:
            return 0;  // No explicit access for present
    }
    return 0;
}

VkImageUsageFlags FrameGraphCompiler::usageToImageUsageFlags(ResourceUsage usage) {
    switch (usage) {
        case ResourceUsage::ColorAttachmentWrite:
        case ResourceUsage::ColorAttachmentBlend:  // Blending also uses color attachment
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        case ResourceUsage::DepthStencilWrite:
        case ResourceUsage::DepthStencilReadOnly:
            return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        case ResourceUsage::ShaderReadOnly:
        case ResourceUsage::InputAttachment:
            return VK_IMAGE_USAGE_SAMPLED_BIT;
        case ResourceUsage::ShaderReadWrite:
        case ResourceUsage::StorageImageWrite:
            return VK_IMAGE_USAGE_STORAGE_BIT;
        case ResourceUsage::TransferSrc:
            return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        case ResourceUsage::TransferDst:
            return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        case ResourceUsage::Present:
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// Topological Sort — Kahn's algorithm
// ═══════════════════════════════════════════════════════════════

bool FrameGraphCompiler::topologicalSort(
    const std::vector<PassDeclaration>& passes,
    const std::vector<ResourceDeclaration>& resources,
    std::vector<uint32_t>& outOrder,
    std::string& outError)
{
    uint32_t passCount = static_cast<uint32_t>(passes.size());

    // Build ordered writer lists per resource (declaration order).
    // Multiple passes may write/blend to the same resource — we need to
    // chain them correctly: color_write → color_blend → color_blend → reader.
    std::unordered_map<uint32_t, std::vector<uint32_t>> resourceWriters;

    for (uint32_t pi = 0; pi < passCount; ++pi) {
        for (const auto& output : passes[pi].outputs) {
            if (output.handle.valid()) {
                resourceWriters[output.handle.index].push_back(pi);
            }
        }
    }

    // Helper: find the most recent writer of a resource that was declared
    // before the given pass (in declaration order).
    auto findPreviousWriter = [&](uint32_t resourceIdx, uint32_t currentPass) -> int32_t {
        auto it = resourceWriters.find(resourceIdx);
        if (it == resourceWriters.end()) return -1;
        int32_t prev = -1;
        for (uint32_t w : it->second) {
            if (w >= currentPass) break;  // Writers are in declaration order
            prev = static_cast<int32_t>(w);
        }
        return prev;
    };

    // Build in-degree and adjacency list
    std::vector<uint32_t> inDegree(passCount, 0);
    std::vector<std::vector<uint32_t>> adj(passCount);  // adj[i] = passes that depend on pass i

    for (uint32_t pi = 0; pi < passCount; ++pi) {
        // Standard input dependencies: depend on most recent previous writer
        for (const auto& input : passes[pi].inputs) {
            if (!input.handle.valid()) continue;
            int32_t writer = findPreviousWriter(input.handle.index, pi);
            if (writer >= 0) {
                adj[writer].push_back(pi);
                inDegree[pi]++;
            }
        }

        // ColorAttachmentBlend outputs also need dependency on previous writer
        // (blending is read-modify-write, requires previous content).
        // This creates proper chains: color_write → color_blend → color_blend
        for (const auto& output : passes[pi].outputs) {
            if (!output.handle.valid()) continue;
            if (output.usage != ResourceUsage::ColorAttachmentBlend) continue;
            int32_t writer = findPreviousWriter(output.handle.index, pi);
            if (writer >= 0) {
                adj[writer].push_back(pi);
                inDegree[pi]++;
            }
        }
    }

    // Kahn's algorithm
    std::queue<uint32_t> ready;
    for (uint32_t i = 0; i < passCount; ++i) {
        if (inDegree[i] == 0) ready.push(i);
    }

    outOrder.clear();
    outOrder.reserve(passCount);

    while (!ready.empty()) {
        uint32_t curr = ready.front();
        ready.pop();
        outOrder.push_back(curr);

        for (uint32_t next : adj[curr]) {
            if (--inDegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (outOrder.size() != passCount) {
        // Find cycle participants for error message
        outError = "FrameGraph: Cycle detected among passes: ";
        for (uint32_t i = 0; i < passCount; ++i) {
            if (inDegree[i] > 0) {
                outError += "'" + passes[i].name + "' ";
            }
        }
        return false;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════
// Dead Pass Culling — backward reachability from Present outputs
// ═══════════════════════════════════════════════════════════════

void FrameGraphCompiler::cullDeadPasses(
    std::vector<uint32_t>& order,
    const std::vector<PassDeclaration>& passes,
    const std::vector<ResourceDeclaration>& resources)
{
    uint32_t passCount = static_cast<uint32_t>(passes.size());

    // Build reverse adjacency: who reads what I write?
    // First, find which pass reads each resource
    std::unordered_map<uint32_t, std::vector<uint32_t>> resourceReaders;  // resource -> passes that read it
    // Track ALL writers to each resource in order (for ColorAttachmentBlend dependency)
    std::unordered_map<uint32_t, std::vector<uint32_t>> resourceWriters;  // resource -> all passes that write it

    for (uint32_t pi = 0; pi < passCount; ++pi) {
        for (const auto& input : passes[pi].inputs) {
            if (input.handle.valid())
                resourceReaders[input.handle.index].push_back(pi);
        }
        for (const auto& output : passes[pi].outputs) {
            if (output.handle.valid())
                resourceWriters[output.handle.index].push_back(pi);
        }
    }

    // Helper to find the previous writer before a given pass index
    auto findPreviousWriter = [&](uint32_t resourceIdx, uint32_t currentPass) -> int32_t {
        auto it = resourceWriters.find(resourceIdx);
        if (it == resourceWriters.end()) return -1;
        const auto& writers = it->second;
        int32_t prevWriter = -1;
        for (uint32_t w : writers) {
            if (w < currentPass) prevWriter = static_cast<int32_t>(w);
            else break;  // Writers are in declaration order
        }
        return prevWriter;
    };

    // Build single-writer map for standard dependency tracking (last writer)
    std::unordered_map<uint32_t, uint32_t> resourceWriter;
    for (const auto& [resIdx, writers] : resourceWriters) {
        if (!writers.empty()) {
            resourceWriter[resIdx] = writers.back();
        }
    }

    // Find root passes: those that write to Present, imported resources, or have side effects
    std::set<uint32_t> live;
    std::queue<uint32_t> worklist;

    for (uint32_t pi = 0; pi < passCount; ++pi) {
        // Passes with side effects are always live (e.g., compute passes that write to SSBOs)
        if (passes[pi].hasSideEffects) {
            if (!live.contains(pi)) {
                live.insert(pi);
                worklist.push(pi);
            }
            continue;
        }

        for (const auto& output : passes[pi].outputs) {
            if (output.usage == ResourceUsage::Present ||
                (output.handle.valid() && output.handle.index < resources.size()
                 && resources[output.handle.index].imported)) {
                if (!live.contains(pi)) {
                    live.insert(pi);
                    worklist.push(pi);
                }
            }
        }
    }

    // Walk backward: if pass P is live, all passes that produce P's inputs are also live
    // Also handle ColorAttachmentBlend outputs which implicitly depend on previous writers
    while (!worklist.empty()) {
        uint32_t pi = worklist.front();
        worklist.pop();

        // Standard input dependencies
        for (const auto& input : passes[pi].inputs) {
            if (!input.handle.valid()) continue;
            auto wit = resourceWriter.find(input.handle.index);
            if (wit != resourceWriter.end() && !live.contains(wit->second)) {
                live.insert(wit->second);
                worklist.push(wit->second);
            }
        }

        // ColorAttachmentBlend outputs also need their previous writer to be live
        // (blending is read-modify-write: requires content from the previous pass)
        for (const auto& output : passes[pi].outputs) {
            if (!output.handle.valid()) continue;
            if (output.usage != ResourceUsage::ColorAttachmentBlend) continue;

            int32_t prevWriter = findPreviousWriter(output.handle.index, pi);
            if (prevWriter >= 0 && !live.contains(static_cast<uint32_t>(prevWriter))) {
                live.insert(static_cast<uint32_t>(prevWriter));
                worklist.push(static_cast<uint32_t>(prevWriter));
            }
        }
    }

    // Filter execution order to only live passes
    std::vector<uint32_t> filtered;
    filtered.reserve(order.size());
    for (uint32_t idx : order) {
        if (live.contains(idx)) {
            filtered.push_back(idx);
        }
    }
    order = std::move(filtered);
}

// ═══════════════════════════════════════════════════════════════
// Physical Resource Creation
// ═══════════════════════════════════════════════════════════════

void FrameGraphCompiler::createPhysicalResources(
    VulkanDevice& device,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<PassDeclaration>& passes,
    std::vector<PhysicalResource>& outResources,
    VkExtent2D referenceExtent,
    const ImportedImageMap& importedImages)
{
    outResources.resize(declarations.size());

    // First pass: compute required usage flags for each resource
    std::vector<VkImageUsageFlags> imageUsages(declarations.size(), 0);
    for (const auto& pass : passes) {
        for (const auto& input : pass.inputs) {
            if (input.handle.valid())
                imageUsages[input.handle.index] |= usageToImageUsageFlags(input.usage);
        }
        for (const auto& output : pass.outputs) {
            if (output.handle.valid())
                imageUsages[output.handle.index] |= usageToImageUsageFlags(output.usage);
        }
    }

    // Create physical resources for non-imported declarations
    for (uint32_t i = 0; i < declarations.size(); ++i) {
        const auto& decl = declarations[i];

        if (decl.imported) {
            // Populate imported resources from import data
            if (decl.kind == ResourceKind::Image) {
                PhysicalImage physImg;
                auto it = importedImages.find(i);
                if (it != importedImages.end()) {
                    physImg.format = it->second.format;
                    physImg.extent = it->second.extent;
                    if (!it->second.images.empty()) {
                        physImg.vkImage = it->second.images[0];
                    }
                    if (!it->second.views.empty()) {
                        physImg.view = it->second.views[0];
                    }
                }
                outResources[i] = std::move(physImg);
            } else {
                outResources[i] = PhysicalBuffer{};
            }
            continue;
        }

        if (decl.kind == ResourceKind::Image) {
            const auto& desc = decl.imageDesc;

            // Resolve size
            uint32_t width  = desc.width  > 0 ? desc.width  : static_cast<uint32_t>(referenceExtent.width * desc.widthScale);
            uint32_t height = desc.height > 0 ? desc.height : static_cast<uint32_t>(referenceExtent.height * desc.heightScale);

            // Combine declared additional usage with usage derived from passes
            VkImageUsageFlags usage = imageUsages[i] | desc.additionalUsage;

            // Determine memory properties based on usage
            VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

            // Determine if this is a depth resource from usage flags or format
            bool isDepthByUsage = (imageUsages[i] & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            bool isDepthByFormat = (desc.format == VK_FORMAT_D16_UNORM ||
                                    desc.format == VK_FORMAT_D32_SFLOAT ||
                                    desc.format == VK_FORMAT_D16_UNORM_S8_UINT ||
                                    desc.format == VK_FORMAT_D24_UNORM_S8_UINT ||
                                    desc.format == VK_FORMAT_D32_SFLOAT_S8_UINT);
            bool isDepth = isDepthByUsage || isDepthByFormat;

            VkFormat format = desc.format;
            if (format == VK_FORMAT_UNDEFINED && isDepth) {
                // Auto-select best available depth format
                format = device.findDepthFormat();
            }

            auto image = std::make_unique<VulkanImage>(
                device, width, height, format,
                VK_IMAGE_TILING_OPTIMAL, usage, memProps
            );

            // Create image view
            VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            image->createImageView(aspect);

            PhysicalImage physImg;
            physImg.vkImage = image->getImage();
            physImg.view = image->getImageView();
            physImg.format = format;
            physImg.extent = {width, height};
            physImg.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            physImg.ownedImage = std::move(image);

            outResources[i] = std::move(physImg);
        } else {
            // Buffer resource
            const auto& desc = decl.bufferDesc;
            VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

            auto buffer = std::make_unique<VulkanBuffer>(
                device, desc.size, desc.usage, memProps
            );

            PhysicalBuffer physBuf;
            physBuf.vkBuffer = buffer->getBuffer();
            physBuf.size = desc.size;
            physBuf.ownedBuffer = std::move(buffer);

            outResources[i] = std::move(physBuf);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Layout Resolution + Barrier Insertion
// ═══════════════════════════════════════════════════════════════

// Helper: resolve queue family index from QueueType
static uint32_t resolveQueueFamily(QueueType type, VulkanDevice& device) {
    switch (type) {
        case QueueType::Compute:
            return device.getComputeQueueFamily();
        case QueueType::Graphics:
        default:
            return device.getGraphicsQueueFamily();
    }
}

void FrameGraphCompiler::resolveLayoutsAndInsertBarriers(
    VulkanDevice& device,
    std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    std::vector<PhysicalResource>& physResources)
{
    // Track current layout of each image resource
    std::vector<VkImageLayout> currentLayouts(physResources.size(), VK_IMAGE_LAYOUT_UNDEFINED);
    std::vector<VkPipelineStageFlags> lastStages(physResources.size(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    std::vector<VkAccessFlags> lastAccess(physResources.size(), 0);
    // Track which queue family last wrote each resource
    std::vector<uint32_t> lastQueueFamily(physResources.size(), VK_QUEUE_FAMILY_IGNORED);

    for (uint32_t execIdx : executionOrder) {
        auto& compiled = compiledPasses[execIdx];
        const auto& passDecl = passes[compiled.declIndex];

        // Store queue type on compiled pass
        compiled.queueType = passDecl.queueType;

        // Process all resource accesses (inputs + outputs)
        auto processAccess = [&](const ResourceAccess& access) {
            if (!access.handle.valid()) return;
            uint32_t ri = access.handle.index;

            auto* physImg = std::get_if<PhysicalImage>(&physResources[ri]);
            if (!physImg) return;  // Buffers don't need layout transitions

            VkImageLayout requiredLayout = usageToLayout(access.usage);
            VkPipelineStageFlags requiredStage = usageToStageMask(access.usage, passDecl.type);
            VkAccessFlags requiredAccess = usageToAccessMask(access.usage);

            if (currentLayouts[ri] != requiredLayout) {
                BarrierInfo barrier;
                barrier.resource = access.handle;
                barrier.oldLayout = currentLayouts[ri];
                barrier.newLayout = requiredLayout;
                barrier.srcStage = lastStages[ri];
                barrier.dstStage = requiredStage;
                barrier.srcAccess = lastAccess[ri];
                barrier.dstAccess = requiredAccess;

                // Check for cross-queue ownership transfer
                uint32_t srcQueue = lastQueueFamily[ri];
                uint32_t dstQueue = resolveQueueFamily(passDecl.queueType, device);
                if (srcQueue != VK_QUEUE_FAMILY_IGNORED && srcQueue != dstQueue) {
                    // Cross-queue transition: set queue family indices
                    barrier.srcQueueFamilyIndex = srcQueue;
                    barrier.dstQueueFamilyIndex = dstQueue;

                    // Create acquire barrier for the destination pass
                    BarrierInfo acquireBarrier = barrier;
                    compiled.acquireBarriers.push_back(acquireBarrier);
                }

                compiled.preBarriers.push_back(barrier);
                currentLayouts[ri] = requiredLayout;
            }

            lastStages[ri] = requiredStage;
            lastAccess[ri] = requiredAccess;
            lastQueueFamily[ri] = resolveQueueFamily(passDecl.queueType, device);
        };

        for (const auto& input : passDecl.inputs) {
            processAccess(input);
        }
        for (const auto& output : passDecl.outputs) {
            processAccess(output);
        }
    }

    // Update physical image layouts to their final state
    for (uint32_t i = 0; i < physResources.size(); ++i) {
        if (auto* img = std::get_if<PhysicalImage>(&physResources[i])) {
            img->currentLayout = currentLayouts[i];
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Render Pass Creation — uses existing RenderPassBuilder
// ═══════════════════════════════════════════════════════════════

void FrameGraphCompiler::createRenderPasses(
    VulkanDevice& device,
    std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    const std::vector<PhysicalResource>& physResources)
{
    for (uint32_t execIdx : executionOrder) {
        auto& compiled = compiledPasses[execIdx];
        const auto& passDecl = passes[compiled.declIndex];

        if (passDecl.type != PassType::Graphics) continue;

        RenderPassBuilder builder;

        // Collect color and depth attachments from outputs
        std::vector<std::string> colorNames;
        std::string depthName;

        for (const auto& output : passDecl.outputs) {
            if (!output.handle.valid()) continue;
            uint32_t ri = output.handle.index;
            const auto* physImg = std::get_if<PhysicalImage>(&physResources[ri]);
            if (!physImg) continue;

            if (output.usage == ResourceUsage::ColorAttachmentWrite ||
                output.usage == ResourceUsage::ColorAttachmentBlend ||
                output.usage == ResourceUsage::Present) {
                std::string attachName = "color_" + std::to_string(colorNames.size());

                // Determine load op:
                // - CLEAR if we have a clear value
                // - LOAD for ColorAttachmentBlend (preserves content for alpha blending)
                // - LOAD if the resource was already written by a previous pass
                VkAttachmentLoadOp loadOp;
                if (output.hasClearValue) {
                    loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                } else if (output.usage == ResourceUsage::ColorAttachmentBlend) {
                    loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Must preserve for blending
                } else {
                    loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                }

                // Determine initial/final layouts from our barrier analysis
                VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                // If there's a barrier transitioning this resource for this pass,
                // the render pass should start from the barrier's newLayout
                for (const auto& barrier : compiled.preBarriers) {
                    if (barrier.resource == output.handle) {
                        initialLayout = barrier.oldLayout;
                        finalLayout = barrier.newLayout;
                        break;
                    }
                }

                // If we're clearing, we can start from UNDEFINED
                if (output.hasClearValue) {
                    initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                }
                // For ColorAttachmentBlend with LOAD op, we MUST use proper layout
                // (UNDEFINED + LOAD = undefined behavior, content may be discarded!)
                else if (output.usage == ResourceUsage::ColorAttachmentBlend &&
                         initialLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
                    initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                }

                AttachmentDescriptor desc;
                desc.name = attachName;
                desc.format = physImg->format;
                desc.loadOp = loadOp;
                desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                desc.initialLayout = initialLayout;
                desc.finalLayout = finalLayout;

                if (output.hasClearValue) {
                    desc.clearValue = output.clearValue;
                }

                builder.addAttachment(desc);
                colorNames.push_back(attachName);

                compiled.clearValues.push_back(
                    output.hasClearValue ? output.clearValue : VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}
                );
            }
            else if (output.usage == ResourceUsage::DepthStencilWrite) {
                depthName = "depth";

                VkAttachmentLoadOp loadOp = output.hasClearValue ?
                    VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;

                VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                if (output.hasClearValue) {
                    initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                }

                AttachmentDescriptor desc;
                desc.name = depthName;
                desc.format = physImg->format;
                desc.loadOp = loadOp;
                desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                desc.initialLayout = initialLayout;
                desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                if (output.hasClearValue) {
                    desc.clearValue = output.clearValue;
                }

                builder.addAttachment(desc);

                VkClearValue defaultDepthClear{};
                defaultDepthClear.depthStencil.depth = 1.0f;
                defaultDepthClear.depthStencil.stencil = 0;
                compiled.clearValues.push_back(
                    output.hasClearValue ? output.clearValue : defaultDepthClear
                );
            }
            else if (output.usage == ResourceUsage::DepthStencilReadOnly) {
                // Read-only depth attachment (for depth testing without writing)
                // 深度之讀 — Reading depth to test against, but not writing
                depthName = "depth";

                // LOAD existing depth values, don't clear
                VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

                // Start from DEPTH_STENCIL_READ_ONLY layout (set by barrier)
                // and stay in read-only layout
                VkImageLayout initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                VkImageLayout finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                AttachmentDescriptor desc;
                desc.name = depthName;
                desc.format = physImg->format;
                desc.loadOp = loadOp;
                desc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Read-only, no need to store
                desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                desc.initialLayout = initialLayout;
                desc.finalLayout = finalLayout;

                builder.addAttachment(desc);

                // Clear value needed for array alignment even though we're loading
                VkClearValue depthClear{};
                depthClear.depthStencil.depth = 1.0f;
                depthClear.depthStencil.stencil = 0;
                compiled.clearValues.push_back(depthClear);
            }
        }

        // Add a single subpass with all attachments
        builder.addBasicSubpass("main", colorNames, depthName);
        builder.addExternalDependency("main");

        // Build the render pass
        auto config = builder.build();
        compiled.renderPass = std::make_shared<VulkanRenderPass>(device, config);

        // Determine pass extent from the first color attachment
        for (const auto& output : passDecl.outputs) {
            if (!output.handle.valid()) continue;
            if (output.usage == ResourceUsage::ColorAttachmentWrite ||
                output.usage == ResourceUsage::ColorAttachmentBlend ||
                output.usage == ResourceUsage::Present) {
                const auto* physImg = std::get_if<PhysicalImage>(&physResources[output.handle.index]);
                if (physImg) {
                    compiled.extent = physImg->extent;
                    break;
                }
            }
        }

        // Fallback for depth-only passes (no color attachments)
        if (compiled.extent.width == 0 || compiled.extent.height == 0) {
            for (const auto& output : passDecl.outputs) {
                if (!output.handle.valid()) continue;
                if (output.usage == ResourceUsage::DepthStencilWrite) {
                    const auto* physImg = std::get_if<PhysicalImage>(&physResources[output.handle.index]);
                    if (physImg) {
                        compiled.extent = physImg->extent;
                        break;
                    }
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Framebuffer Creation
// ═══════════════════════════════════════════════════════════════

void FrameGraphCompiler::createFramebuffers(
    VulkanDevice& device,
    std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    const std::vector<PhysicalResource>& physResources,
    uint32_t swapchainImageCount,
    const ImportedImageMap& importedImages)
{
    for (uint32_t execIdx : executionOrder) {
        auto& compiled = compiledPasses[execIdx];
        const auto& passDecl = passes[compiled.declIndex];

        if (passDecl.type != PassType::Graphics || !compiled.renderPass) continue;

        // Check if any attachment uses an imported resource with per-swapchain views
        bool usesPerSwapchainViews = false;
        for (const auto& output : passDecl.outputs) {
            if (!output.handle.valid()) continue;
            auto importIt = importedImages.find(output.handle.index);
            if (importIt != importedImages.end() && importIt->second.views.size() > 1) {
                usesPerSwapchainViews = true;
                break;
            }
        }

        uint32_t fbCount = usesPerSwapchainViews ? swapchainImageCount : 1;
        compiled.framebuffers.resize(fbCount);

        for (uint32_t fi = 0; fi < fbCount; ++fi) {
            // Collect attachment views in order
            std::vector<VkImageView> views;

            for (const auto& output : passDecl.outputs) {
                if (!output.handle.valid()) continue;
                if (output.usage != ResourceUsage::ColorAttachmentWrite &&
                    output.usage != ResourceUsage::ColorAttachmentBlend &&
                    output.usage != ResourceUsage::Present &&
                    output.usage != ResourceUsage::DepthStencilWrite &&
                    output.usage != ResourceUsage::DepthStencilReadOnly) continue;

                const auto* physImg = std::get_if<PhysicalImage>(&physResources[output.handle.index]);
                if (!physImg) continue;

                // For imported resources, use per-swapchain-image view
                VkImageView view = physImg->view;
                auto importIt = importedImages.find(output.handle.index);
                if (importIt != importedImages.end() && fi < importIt->second.views.size()) {
                    view = importIt->second.views[fi];
                }

                if (view != VK_NULL_HANDLE) {
                    views.push_back(view);
                }
            }

            if (!views.empty() && compiled.extent.width > 0 && compiled.extent.height > 0) {
                compiled.framebuffers[fi] = compiled.renderPass->createFramebuffer(
                    compiled.extent, views
                );
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Main Compile Entry Point
// ═══════════════════════════════════════════════════════════════

FrameGraphCompiler::CompileResult FrameGraphCompiler::compile(
    VulkanDevice& device,
    const FrameGraphBuilder& builder,
    VkExtent2D referenceExtent,
    uint32_t swapchainImageCount,
    const ImportedImageMap& importedImages,
    uint32_t maxFramesInFlight,
    const std::unordered_map<std::string, std::shared_ptr<VulkanPipeline>>& manualPipelines)
{
    if (!m_logger) {
        m_logger = new Logger("framegraph_compiler.log");
    }

    CompileResult result;
    result.valid = false;

    const auto& resources = builder.getResourceDeclarations();
    const auto& passes = builder.getPassDeclarations();

    if (passes.empty()) {
        result.errorMessage = "FrameGraph: No passes declared";
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }

    m_logger->log(LogLevel::Info, "Compiling frame graph: %zu passes, %zu resources",
                  passes.size(), resources.size());

    // Stage 1: Topological sort
    if (!topologicalSort(passes, resources, result.executionOrder, result.errorMessage)) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }
    m_logger->log(LogLevel::Info, "Topological sort complete: %zu passes in order",
                  result.executionOrder.size());

    // Stage 2: Dead pass culling
    size_t beforeCull = result.executionOrder.size();
    cullDeadPasses(result.executionOrder, passes, resources);
    m_logger->log(LogLevel::Info, "Dead pass culling: %zu -> %zu passes",
                  beforeCull, result.executionOrder.size());

    // Stage 3: Create physical resources
    createPhysicalResources(device, resources, passes, result.physicalResources, referenceExtent, importedImages);
    m_logger->log(LogLevel::Info, "Physical resources created: %zu", result.physicalResources.size());

    // Stage 3.5: Create samplers from JSON declarations
    const auto& samplerDescs = builder.getSamplers();
    if (!samplerDescs.empty()) {
        createSamplers(device, samplerDescs, result.samplers);
        m_logger->log(LogLevel::Info, "Samplers created: %zu", result.samplers.size());
    }

    // Stage 4: Initialize compiled passes
    result.compiledPasses.resize(passes.size());
    for (uint32_t i = 0; i < passes.size(); ++i) {
        result.compiledPasses[i].declIndex = i;
    }

    // Stage 4.5: Resolve entityDataBindings for geometry passes
    const auto& entityDataBindings = builder.getEntityDataBindings();
    for (uint32_t i = 0; i < passes.size(); ++i) {
        const auto& passDecl = passes[i];
        auto& compiled = result.compiledPasses[i];

        // Check if this pass references an entityDataBinding
        if (!passDecl.execution.entityDataBinding.empty()) {
            // Look up the binding by name
            for (const auto& binding : entityDataBindings) {
                if (binding.name == passDecl.execution.entityDataBinding) {
                    compiled.entityDataBinding = binding;
                    compiled.hasEntityDataBinding = true;
                    m_logger->log(LogLevel::Info, "Pass '%s': resolved entityDataBinding '%s' (perDraw='%s', material='%s')",
                        passDecl.name.c_str(), binding.name.c_str(),
                        binding.perDraw.layoutRef.c_str(), binding.material.layoutRef.c_str());
                    break;
                }
            }
            if (!compiled.hasEntityDataBinding) {
                m_logger->log(LogLevel::Warning, "Pass '%s': entityDataBinding '%s' not found!",
                    passDecl.name.c_str(), passDecl.execution.entityDataBinding.c_str());
            }
        }
    }

    // Stage 5: Layout resolution + barrier insertion (with queue ownership transfers)
    resolveLayoutsAndInsertBarriers(
        device, result.compiledPasses, result.executionOrder, passes, result.physicalResources);
    m_logger->log(LogLevel::Info, "Layout resolution and barriers complete");

    // Stage 6: Create render passes
    createRenderPasses(device, result.compiledPasses, result.executionOrder,
                       passes, result.physicalResources);
    m_logger->log(LogLevel::Info, "Render passes created");

    // Stage 7: Create framebuffers
    createFramebuffers(device, result.compiledPasses, result.executionOrder,
                       passes, result.physicalResources, swapchainImageCount, importedImages);
    m_logger->log(LogLevel::Info, "Framebuffers created");

    // Stage 8: Create descriptor set layouts
    const auto& layoutDescs = builder.getDescriptorSetLayouts();
    if (!layoutDescs.empty()) {
        createDescriptorSetLayouts(device, result.compiledPasses, result.executionOrder,
                                   passes, layoutDescs, result.namedDescriptorSets, maxFramesInFlight);
        m_logger->log(LogLevel::Info, "Descriptor set layouts created: %zu", result.namedDescriptorSets.size());

        // Stage 8.5: Perform auto-binding for descriptor sets
        performAutoBindings(device, builder, layoutDescs, result.namedDescriptorSets,
                           result.physicalResources, result.samplers, maxFramesInFlight);
        m_logger->log(LogLevel::Info, "Auto-bindings performed");
    }

    // Stage 9: Create pipelines (auto-create from PipelineDesc, skip manual overrides)
    createPipelines(device, result.compiledPasses, result.executionOrder,
                    passes, result.physicalResources, manualPipelines,
                    builder.getVertexFormatRegistry());
    m_logger->log(LogLevel::Info, "Pipelines created");

    // Stage 10: Generate multi-queue execution batches
    generateQueueBatches(device, result.compiledPasses, result.executionOrder, passes, result.queueBatches);
    m_logger->log(LogLevel::Info, "Queue batches generated: %zu batches, %zu sync points",
                  result.queueBatches.batches.size(), result.queueBatches.syncPoints.size());

    // Stage 11: Compile buffer layouts (declarative dot-path)
    const auto& bufferLayoutDescs = builder.getBufferLayouts();
    if (!bufferLayoutDescs.empty()) {
        compileBufferLayouts(bufferLayoutDescs, result.bufferLayouts);
        m_logger->log(LogLevel::Info, "Buffer layouts compiled: %zu layouts", result.bufferLayouts.size());
    }

    result.valid = true;
    m_logger->log(LogLevel::Info, "Frame graph compilation successful");

    return result;
}

// ═══════════════════════════════════════════════════════════════
// Stage 8: Descriptor Set Layout Creation
// 約束為位 — Constraints establish position
// ═══════════════════════════════════════════════════════════════

void FrameGraphCompiler::createDescriptorSetLayouts(
    VulkanDevice& device,
    std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    const std::vector<DescriptorSetLayoutDesc>& layoutDescs,
    std::unordered_map<std::string, std::shared_ptr<VulkanDescriptorSet>>& outNamedSets,
    uint32_t maxFramesInFlight)
{
    // Build a lookup map: layout name -> DescriptorSetLayoutDesc
    std::unordered_map<std::string, const DescriptorSetLayoutDesc*> layoutMap;
    for (const auto& desc : layoutDescs) {
        layoutMap[desc.name] = &desc;
    }

    // Create VulkanDescriptorSet for each unique layout name referenced by any pass
    for (const auto& desc : layoutDescs) {
        // Build a DescriptorLayoutBuilder from the JSON-declared bindings
        DescriptorLayoutBuilder builder;

        for (const auto& binding : desc.bindings) {
            VkDescriptorType descType = JsonUtils::stringToDescriptorType(binding.type);
            VkShaderStageFlags stageFlags = JsonUtils::stringsToShaderStages(binding.stages);
            std::string bindingName = binding.name.empty()
                ? "binding_" + std::to_string(binding.binding)
                : binding.name;

            // Determine whether this is a buffer or image type
            switch (descType) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                    BufferBinding buf;
                    buf.name = bindingName;
                    buf.type = descType;
                    buf.stages = stageFlags;
                    buf.binding = binding.binding;
                    buf.count = binding.count;
                    builder.addBuffer(buf);
                    break;
                }
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                case VK_DESCRIPTOR_TYPE_SAMPLER: {
                    ImageBinding img;
                    img.name = bindingName;
                    img.type = descType;
                    img.stages = stageFlags;
                    img.binding = binding.binding;
                    img.count = binding.count;
                    builder.addImage(img);
                    break;
                }
                default:
                    m_logger->log(LogLevel::Warning, "Unsupported descriptor type for binding '%s'",
                                  bindingName.c_str());
                    break;
            }
        }

        auto config = builder.build();
        auto descriptorSet = std::make_shared<VulkanDescriptorSet>(device, config, maxFramesInFlight);
        outNamedSets[desc.name] = descriptorSet;

        m_logger->log(LogLevel::Info, "  Created descriptor set layout '%s' with %zu bindings",
                      desc.name.c_str(), desc.bindings.size());
    }

    // Wire descriptor sets to compiled passes based on their descriptorSetRefs
    for (uint32_t execIdx : executionOrder) {
        auto& compiled = compiledPasses[execIdx];
        const auto& passDecl = passes[compiled.declIndex];

        compiled.descriptorSetLayouts.clear();
        compiled.descriptorSets.clear();

        for (const auto& refName : passDecl.descriptorSetRefs) {
            auto it = outNamedSets.find(refName);
            if (it != outNamedSets.end()) {
                compiled.descriptorSetLayouts.push_back(it->second->getLayout());
                compiled.descriptorSets.push_back(it->second);
            } else {
                m_logger->log(LogLevel::Warning,
                    "Pass '%s' references descriptor set layout '%s' which has no definition",
                    passDecl.name.c_str(), refName.c_str());
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Stage 9: Automatic Pipeline Creation
// 朱雀之焰煉其精純 — The Vermilion Bird's flame refines to purity
// ═══════════════════════════════════════════════════════════════

VkCullModeFlags FrameGraphCompiler::stringToCullMode(const std::string& str) {
    if (str == "none")           return VK_CULL_MODE_NONE;
    if (str == "front")          return VK_CULL_MODE_FRONT_BIT;
    if (str == "back")           return VK_CULL_MODE_BACK_BIT;
    if (str == "front_and_back") return VK_CULL_MODE_FRONT_AND_BACK;
    return VK_CULL_MODE_BACK_BIT;  // default
}

VkPrimitiveTopology FrameGraphCompiler::stringToTopology(const std::string& str) {
    if (str == "triangle_list")  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (str == "triangle_strip") return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    if (str == "line_list")      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    if (str == "line_strip")     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    if (str == "point_list")     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  // default
}

void FrameGraphCompiler::createPipelines(
    VulkanDevice& device,
    std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    const std::vector<PhysicalResource>& physResources,
    const std::unordered_map<std::string, std::shared_ptr<VulkanPipeline>>& manualOverrides,
    const VertexFormatRegistry& vertexFormats)
{
    for (uint32_t execIdx : executionOrder) {
        auto& compiled = compiledPasses[execIdx];
        const auto& passDecl = passes[compiled.declIndex];

        // ── Compute pass: auto-create compute pipeline ──
        if (passDecl.type == PassType::Compute) {
            const auto& pd = passDecl.pipelineDesc;
            if (!pd.computeShader.empty()) {
                // Check for manual override (compute passes can also be overridden)
                auto overrideIt = manualOverrides.find(passDecl.name);
                if (overrideIt != manualOverrides.end()) {
                    // Manual overrides for compute passes are handled by user code
                    m_logger->log(LogLevel::Info, "  Compute pass '%s' has manual override, skipping auto-create",
                                  passDecl.name.c_str());
                    continue;
                }

                // Build push constant ranges from declaration
                std::vector<VkPushConstantRange> pushRanges;
                for (const auto& pc : passDecl.pushConstants) {
                    VkPushConstantRange range{};
                    range.stageFlags = JsonUtils::stringsToShaderStages(pc.stages);
                    range.size = pc.size;
                    range.offset = pc.offset;
                    pushRanges.push_back(range);
                }

                try {
                    compiled.computePipeline = std::make_shared<VulkanComputePipeline>(
                        device,
                        pd.computeShader,
                        compiled.descriptorSetLayouts,
                        pushRanges
                    );
                    compiled.pipelineLayout = compiled.computePipeline->getLayout();
                    m_logger->log(LogLevel::Info, "  Auto-created compute pipeline for pass '%s'",
                                  passDecl.name.c_str());
                } catch (const std::exception& e) {
                    m_logger->log(LogLevel::Warning,
                        "  Failed to auto-create compute pipeline for pass '%s': %s",
                        passDecl.name.c_str(), e.what());
                }
            }
            continue;  // Skip graphics pipeline creation for compute passes
        }

        // Skip non-graphics passes
        if (passDecl.type != PassType::Graphics) continue;

        // Skip if no render pass (shouldn't happen for graphics, but be safe)
        if (!compiled.renderPass) continue;

        // Check for manual override
        auto overrideIt = manualOverrides.find(passDecl.name);
        if (overrideIt != manualOverrides.end()) {
            compiled.pipeline = overrideIt->second;
            compiled.pipelineLayout = overrideIt->second->getLayout();
            m_logger->log(LogLevel::Info, "  Using manual pipeline for pass '%s'",
                          passDecl.name.c_str());
            continue;
        }

        // Skip if no shaders specified — user manages pipelines manually
        const auto& pd = passDecl.pipelineDesc;
        if (pd.vertexShader.empty()) continue;

        // Build pipeline from PipelineDesc
        PipelineStateBuilder builder;

        // Shaders
        if (!pd.fragmentShader.empty()) {
            builder.withShaders(pd.vertexShader, pd.fragmentShader);
        } else {
            // Depth-only pass: vertex shader only (fragment shader omitted)
            // PipelineStateBuilder needs vertex shader at minimum
            builder.withShaders(pd.vertexShader, "");
        }

        // Vertex input — check declarative registry first, then fall back to hardcoded
        if (pd.vertexInput != "none") {
            VkVertexInputBindingDescription vtxBinding{};
            std::vector<VkVertexInputAttributeDescription> vtxAttributes;

            if (vertexFormats.hasFormat(pd.vertexInput) &&
                vertexFormats.getVertexInputDescriptions(pd.vertexInput, vtxBinding, vtxAttributes)) {
                // Use declarative vertex format from JSON
                builder.withCustomVertexInput({vtxBinding}, vtxAttributes);
            } else if (pd.vertexInput == "default") {
                // Backward compatibility: use hardcoded Vertex struct
                builder.withVertexType<Vertex>();
            }
            // else: unknown format name — no vertex input (will likely cause validation error)
        }
        // "none" = no vertex input (fullscreen triangle), don't call withVertexType

        // Topology
        builder.withTopology(stringToTopology(pd.topology));

        // Rasterization
        builder.withCulling(stringToCullMode(pd.cullMode));
        if (pd.wireframe) builder.withWireframe();

        // Depth
        builder.withDepthTest(pd.depthTest);
        builder.withDepthWrite(pd.depthWrite);

        // Blending
        if (pd.blending == "alpha") builder.withAlphaBlending();
        else if (pd.blending == "additive") builder.withAdditiveBlending();

        // Count color attachments for MRT support
        uint32_t colorAttachmentCount = 0;
        for (const auto& output : passDecl.outputs) {
            if (output.usage == ResourceUsage::ColorAttachmentWrite ||
                output.usage == ResourceUsage::ColorAttachmentBlend ||
                output.usage == ResourceUsage::Present) {
                colorAttachmentCount++;
            }
        }
        if (colorAttachmentCount > 0) {
            builder.withColorAttachmentCount(colorAttachmentCount);
        }

        // Always use dynamic viewport/scissor
        builder.withDynamicViewport();
        builder.withDynamicScissor();

        // Descriptor set layouts from auto-created sets (Feature 1)
        for (auto layout : compiled.descriptorSetLayouts) {
            builder.withDescriptorSetLayout(layout);
        }

        // Push constant ranges
        for (const auto& pc : passDecl.pushConstants) {
            VkShaderStageFlags stages = JsonUtils::stringsToShaderStages(pc.stages);
            builder.withPushConstants(stages, pc.size, pc.offset);
        }

        // Build the pipeline
        try {
            compiled.pipeline = builder.buildPipeline(device, *compiled.renderPass, compiled.extent);
            compiled.pipelineLayout = compiled.pipeline->getLayout();
            m_logger->log(LogLevel::Info, "  Auto-created pipeline for pass '%s'",
                          passDecl.name.c_str());
        } catch (const std::exception& e) {
            m_logger->log(LogLevel::Warning,
                "  Failed to auto-create pipeline for pass '%s': %s",
                passDecl.name.c_str(), e.what());
            // Non-fatal: user can still bind a pipeline in their callback
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Stage 10: Multi-Queue Batch Generation
// 雙流並行  計算與繪製各得其道
// Dual streams in parallel — compute and graphics each find their Way
// ═══════════════════════════════════════════════════════════════

void FrameGraphCompiler::generateQueueBatches(
    VulkanDevice& device,
    const std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    QueueSubmitBatch& outBatches)
{
    outBatches.batches.clear();
    outBatches.syncPoints.clear();

    if (executionOrder.empty()) return;

    // If no dedicated compute queue, everything runs on graphics — one batch, no sync
    if (!device.hasDedicatedComputeQueue()) {
        std::vector<uint32_t> allPasses(executionOrder.begin(), executionOrder.end());
        outBatches.batches.push_back({QueueType::Graphics, allPasses});
        m_logger->log(LogLevel::Info, "  Single-queue mode: all %zu passes in one batch", allPasses.size());
        return;
    }

    // Group consecutive same-queue passes into batches
    QueueType currentQueue = compiledPasses[executionOrder[0]].queueType;
    std::vector<uint32_t> currentBatch;
    uint64_t timelineValue = 1;

    for (uint32_t passIdx : executionOrder) {
        QueueType passQueue = compiledPasses[passIdx].queueType;

        if (passQueue != currentQueue) {
            // Queue transition detected — flush current batch and add sync point
            outBatches.batches.push_back({currentQueue, currentBatch});

            // Create sync point: previous queue signals, new queue waits
            SyncPoint sync;
            sync.signalQueue = resolveQueueFamily(currentQueue, device);
            sync.waitQueue = resolveQueueFamily(passQueue, device);
            sync.timelineValue = timelineValue++;
            outBatches.syncPoints.push_back(sync);

            currentBatch.clear();
            currentQueue = passQueue;
        }

        currentBatch.push_back(passIdx);
    }

    // Flush last batch
    if (!currentBatch.empty()) {
        outBatches.batches.push_back({currentQueue, currentBatch});
    }

    m_logger->log(LogLevel::Info, "  Multi-queue: %zu batches, %zu sync points",
                  outBatches.batches.size(), outBatches.syncPoints.size());
}

// ═══════════════════════════════════════════════════════════════
// Stage 3.5: Sampler Creation
// 簡而明 — Simple yet clear
// ═══════════════════════════════════════════════════════════════

void FrameGraphCompiler::createSamplers(
    VulkanDevice& device,
    const std::vector<SamplerDesc>& samplerDescs,
    std::unordered_map<std::string, VkSampler>& outSamplers)
{
    for (const auto& desc : samplerDescs) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

        // Filtering
        samplerInfo.magFilter = JsonUtils::stringToFilter(desc.magFilter);
        samplerInfo.minFilter = JsonUtils::stringToFilter(desc.minFilter);
        samplerInfo.mipmapMode = JsonUtils::stringToMipmapMode(desc.mipmapMode);

        // Address modes
        if (!desc.addressMode.empty()) {
            VkSamplerAddressMode mode = JsonUtils::stringToAddressMode(desc.addressMode);
            samplerInfo.addressModeU = mode;
            samplerInfo.addressModeV = mode;
            samplerInfo.addressModeW = mode;
        } else {
            samplerInfo.addressModeU = JsonUtils::stringToAddressMode(desc.addressModeU);
            samplerInfo.addressModeV = JsonUtils::stringToAddressMode(desc.addressModeV);
            samplerInfo.addressModeW = JsonUtils::stringToAddressMode(desc.addressModeW);
        }

        // Border color
        samplerInfo.borderColor = JsonUtils::stringToBorderColor(desc.borderColor);

        // Anisotropy
        samplerInfo.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = desc.maxAnisotropy;

        // Depth comparison (for shadow sampling)
        samplerInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
        samplerInfo.compareOp = JsonUtils::stringToCompareOp(desc.compareOp);

        // LOD control
        samplerInfo.minLod = desc.minLod;
        samplerInfo.maxLod = desc.maxLod;
        samplerInfo.mipLodBias = desc.mipLodBias;

        // Unnormalized coordinates (rarely used)
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        VkSampler sampler;
        if (vkCreateSampler(device.getLogicalDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
            m_logger->log(LogLevel::Error, "Failed to create sampler '%s'", desc.name.c_str());
            continue;
        }

        outSamplers[desc.name] = sampler;
        m_logger->log(LogLevel::Info, "  Created sampler '%s' (filter=%s, address=%s%s)",
                      desc.name.c_str(), desc.magFilter.c_str(),
                      desc.addressMode.empty() ? desc.addressModeU.c_str() : desc.addressMode.c_str(),
                      desc.compareEnable ? ", compareEnable" : "");
    }
}

// ═══════════════════════════════════════════════════════════════
// Stage 8.5: Auto-Binding for Descriptor Sets
// 約束為位  殘差為時 — Constraints establish position, residuals reveal timing
// ═══════════════════════════════════════════════════════════════

void FrameGraphCompiler::performAutoBindings(
    VulkanDevice& device,
    const FrameGraphBuilder& builder,
    const std::vector<DescriptorSetLayoutDesc>& layoutDescs,
    std::unordered_map<std::string, std::shared_ptr<VulkanDescriptorSet>>& namedSets,
    const std::vector<PhysicalResource>& physicalResources,
    const std::unordered_map<std::string, VkSampler>& samplers,
    uint32_t maxFramesInFlight)
{
    for (const auto& layoutDesc : layoutDescs) {
        auto setIt = namedSets.find(layoutDesc.name);
        if (setIt == namedSets.end()) continue;

        auto& descriptorSet = setIt->second;

        for (const auto& binding : layoutDesc.bindings) {
            // Check if this binding has auto-bind configuration
            bool hasAutoBind = !binding.autoBindResource.empty() ||
                               !binding.autoBindSampler.empty() ||
                               !binding.autoBindBuffer.empty();

            if (!hasAutoBind) continue;

            // Get binding name
            std::string bindingName = binding.name.empty()
                ? "binding_" + std::to_string(binding.binding)
                : binding.name;

            // Handle image bindings (autoBindResource + autoBindSampler)
            if (!binding.autoBindResource.empty()) {
                // Look up the physical resource
                auto resourceHandle = builder.getResource(binding.autoBindResource);
                if (!resourceHandle.valid() || resourceHandle.index >= physicalResources.size()) {
                    m_logger->log(LogLevel::Warning,
                        "Auto-bind: Resource '%s' not found for binding '%s' in layout '%s'",
                        binding.autoBindResource.c_str(), bindingName.c_str(), layoutDesc.name.c_str());
                    continue;
                }

                const auto* physImg = std::get_if<PhysicalImage>(&physicalResources[resourceHandle.index]);
                if (!physImg) {
                    m_logger->log(LogLevel::Warning,
                        "Auto-bind: Resource '%s' is not an image for binding '%s'",
                        binding.autoBindResource.c_str(), bindingName.c_str());
                    continue;
                }

                // Look up sampler if specified
                VkSampler sampler = VK_NULL_HANDLE;
                if (!binding.autoBindSampler.empty()) {
                    auto samplerIt = samplers.find(binding.autoBindSampler);
                    if (samplerIt != samplers.end()) {
                        sampler = samplerIt->second;
                    } else {
                        m_logger->log(LogLevel::Warning,
                            "Auto-bind: Sampler '%s' not found for binding '%s'",
                            binding.autoBindSampler.c_str(), bindingName.c_str());
                    }
                }

                // Determine image layout based on descriptor type
                VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkDescriptorType descType = JsonUtils::stringToDescriptorType(binding.type);
                if (descType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                    layout = VK_IMAGE_LAYOUT_GENERAL;
                }

                // Bind to all frames
                for (uint32_t frame = 0; frame < maxFramesInFlight; ++frame) {
                    ImageResource imgRes{physImg->view, sampler, layout};
                    descriptorSet->bindImage(bindingName, frame, imgRes);
                    descriptorSet->updateSet(frame);
                }

                m_logger->log(LogLevel::Info, "  Auto-bound '%s' -> resource '%s'%s",
                              bindingName.c_str(), binding.autoBindResource.c_str(),
                              sampler ? (" + sampler '" + binding.autoBindSampler + "'").c_str() : "");
            }

            // Handle buffer bindings (autoBindBuffer)
            // Note: Buffer auto-binding requires registered UBOs - handled at runtime
            if (!binding.autoBindBuffer.empty()) {
                m_logger->log(LogLevel::Info, "  Auto-bind buffer '%s' -> '%s' (runtime binding)",
                              bindingName.c_str(), binding.autoBindBuffer.c_str());
                // Buffer binding will be done at runtime when RenderGraph.registerUniformBuffer is called
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Stage 11: Buffer Layout Compilation
// 緩衝之構 — The structure of buffers arises from declaration
// Uses dot-path resolution for automatic data binding
// ═══════════════════════════════════════════════════════════════

namespace {

/// Get size of a BufferFieldType in bytes
uint32_t getFieldTypeSize(BufferFieldType type) {
    switch (type) {
        case BufferFieldType::Float:  return 4;
        case BufferFieldType::Double: return 8;
        case BufferFieldType::Int:    return 4;
        case BufferFieldType::UInt:   return 4;
        case BufferFieldType::Bool:   return 4;  // GLSL bool is 4 bytes
        case BufferFieldType::Vec2:   return 8;
        case BufferFieldType::Vec3:   return 12;
        case BufferFieldType::Vec4:   return 16;
        case BufferFieldType::IVec2:  return 8;
        case BufferFieldType::IVec3:  return 12;
        case BufferFieldType::IVec4:  return 16;
        case BufferFieldType::UVec2:  return 8;
        case BufferFieldType::UVec3:  return 12;
        case BufferFieldType::UVec4:  return 16;
        case BufferFieldType::Mat2:   return 16;
        case BufferFieldType::Mat3:   return 36;
        case BufferFieldType::Mat4:   return 64;
    }
    return 4;  // Default fallback
}

/// std140 alignment rules (OpenGL 4.6 spec, section 7.6.2.2)
/// scalar: N (4 for float/int), vec2: 2N, vec3: 4N, vec4: 4N, matNxM: column vec alignment
uint32_t getStd140Alignment(BufferFieldType type) {
    switch (type) {
        case BufferFieldType::Float:
        case BufferFieldType::Int:
        case BufferFieldType::UInt:
        case BufferFieldType::Bool:   return 4;
        case BufferFieldType::Double: return 8;
        case BufferFieldType::Vec2:
        case BufferFieldType::IVec2:
        case BufferFieldType::UVec2:  return 8;
        case BufferFieldType::Vec3:
        case BufferFieldType::IVec3:
        case BufferFieldType::UVec3:  return 16;  // vec3 aligned to vec4!
        case BufferFieldType::Vec4:
        case BufferFieldType::IVec4:
        case BufferFieldType::UVec4:  return 16;
        case BufferFieldType::Mat2:   return 16;  // columns are vec2, but aligned to vec4
        case BufferFieldType::Mat3:   return 16;  // columns are vec3, aligned to vec4
        case BufferFieldType::Mat4:   return 16;  // columns are vec4, aligned to vec4
    }
    return 4;
}

/// std140 padded size (accounts for column padding in matrices)
uint32_t getStd140PaddedSize(BufferFieldType type) {
    switch (type) {
        // Scalars and vectors use their natural size
        case BufferFieldType::Float:
        case BufferFieldType::Int:
        case BufferFieldType::UInt:
        case BufferFieldType::Bool:   return 4;
        case BufferFieldType::Double: return 8;
        case BufferFieldType::Vec2:
        case BufferFieldType::IVec2:
        case BufferFieldType::UVec2:  return 8;
        case BufferFieldType::Vec3:
        case BufferFieldType::IVec3:
        case BufferFieldType::UVec3:  return 12;  // 12 bytes data (padding handled by next alignment)
        case BufferFieldType::Vec4:
        case BufferFieldType::IVec4:
        case BufferFieldType::UVec4:  return 16;
        // Matrices: each column padded to vec4 alignment in std140
        case BufferFieldType::Mat2:   return 2 * 16;  // 2 columns x 16 bytes (vec2 padded to vec4)
        case BufferFieldType::Mat3:   return 3 * 16;  // 3 columns x 16 bytes (vec3 padded to vec4)
        case BufferFieldType::Mat4:   return 4 * 16;  // 4 columns x 16 bytes (vec4)
    }
    return 4;
}

} // anonymous namespace

// CompiledBufferLayout is now in Shoonyakasha::FrameGraph namespace
VkShaderStageFlags CompiledBufferLayout::getShaderStages() const {
    return JsonUtils::stringsToShaderStages(binding.stages);
}

void FrameGraphCompiler::compileBufferLayouts(
    const std::vector<BufferLayoutDesc>& layoutDescs,
    std::unordered_map<std::string, CompiledBufferLayout>& outLayouts)
{
    for (const auto& desc : layoutDescs) {
        CompiledBufferLayout compiled;
        compiled.name = desc.name;
        compiled.usage = desc.usage;
        compiled.updateFrequency = desc.updateFrequency;
        compiled.binding = desc.binding;
        compiled.textures = desc.textures;

        // For descriptor_set type (texture groups), no size calculation needed
        if (desc.usage == BufferUsageType::DescriptorSet) {
            compiled.totalSize = 0;
            outLayouts[desc.name] = std::move(compiled);

            m_logger->log(LogLevel::Info, "  Compiled buffer layout '%s' (descriptor_set, %zu textures)",
                          desc.name.c_str(), desc.textures.size());
            continue;
        }

        // For buffer types, skip if no fields
        if (desc.fields.empty()) {
            m_logger->log(LogLevel::Warning, "  Buffer layout '%s' has no fields, skipping",
                          desc.name.c_str());
            continue;
        }

        // Copy fields first so we can set computed offsets
        compiled.fields = desc.fields;

        // Calculate total size from fields with packing-aware alignment
        bool useStd140 = (desc.packing == BufferPackingRule::Std140);

        uint32_t currentOffset = 0;
        for (auto& field : compiled.fields) {
            uint32_t arrayMultiplier = (field.arrayCount > 0) ? field.arrayCount : 1;

            if (useStd140) {
                uint32_t alignment = getStd140Alignment(field.type);

                if (arrayMultiplier > 1) {
                    // std140 rule: array elements are rounded up to vec4 alignment (16 bytes)
                    alignment = std::max(alignment, 16u);
                }

                currentOffset = (currentOffset + alignment - 1) & ~(alignment - 1);
                field.offset = currentOffset;

                uint32_t fieldSize = getStd140PaddedSize(field.type);

                if (arrayMultiplier > 1) {
                    // Array stride: element size rounded up to 16 bytes (std140 rule)
                    uint32_t arrayStride = (fieldSize + 15u) & ~15u;
                    field.arrayStride = arrayStride;
                    currentOffset += arrayStride * arrayMultiplier;
                } else {
                    currentOffset += fieldSize;
                }
            } else {
                // Scalar packing: no alignment padding, use explicit offset or pack tightly
                if (field.offset != 0) {
                    currentOffset = field.offset;
                }
                field.offset = currentOffset;
                uint32_t fieldSize = getFieldTypeSize(field.type);
                currentOffset += fieldSize * arrayMultiplier;
            }
        }
        compiled.totalSize = currentOffset;

        // Validate push constant size against Vulkan minimum guarantee
        if (desc.usage == BufferUsageType::PushConstant && compiled.totalSize > 128) {
            m_logger->log(LogLevel::Warning,
                "  Push constant layout '%s' is %u bytes, exceeding Vulkan minimum guarantee "
                "(128 bytes). May fail on some hardware. Consider moving data to a UBO.",
                desc.name.c_str(), compiled.totalSize);
        }

        // Classify sources
        compiled.hasSceneSources = false;
        compiled.hasEntitySources = false;
        compiled.hasConstSources = false;

        for (const auto& field : compiled.fields) {
            if (!field.source.empty()) {
                if (field.source.starts_with("scene.")) {
                    compiled.hasSceneSources = true;
                } else if (field.source.starts_with("entity.")) {
                    compiled.hasEntitySources = true;
                } else if (field.source.starts_with("const.")) {
                    compiled.hasConstSources = true;
                }
            }
        }

        const char* usageStr = desc.usage == BufferUsageType::PushConstant ? "push_constant" :
                               desc.usage == BufferUsageType::UniformBuffer ? "uniform_buffer" :
                               desc.usage == BufferUsageType::StorageBuffer ? "storage_buffer" : "unknown";
        const char* freqStr = desc.updateFrequency == BufferUpdateFrequency::PerFrame ? ", per_frame" : "";

        outLayouts[desc.name] = std::move(compiled);

        m_logger->log(LogLevel::Info, "  Compiled buffer layout '%s' (%s%s, %u bytes, %zu fields%s%s)",
                      desc.name.c_str(), usageStr, freqStr,
                      currentOffset, desc.fields.size(),
                      useStd140 ? ", std140" : ", scalar",
                      compiled.usesDotPathSources() ? ", dot-path sources" : "");
    }
}

} // namespace FrameGraph
} // namespace Shoonyakasha
