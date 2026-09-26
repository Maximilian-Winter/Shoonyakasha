//
// Shoonyakasha Engine - Frame Graph Scheduling
//

#include "Vulkan/FrameGraph/FrameGraphSchedule.h"
#include "GPU/VkFormatUtils.h"

#include <algorithm>
#include <map>
#include <functional>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_set>

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Resources
// ═══════════════════════════════════════════════════════════════

std::vector<ScheduledResource> describeResources(
    const std::vector<ResourceDeclaration>& resources,
    const std::vector<VkExtent2D>& extents)
{
    std::vector<ScheduledResource> out(resources.size());
    for (size_t i = 0; i < resources.size(); ++i) {
        const auto& decl = resources[i];
        auto& r = out[i];
        if (decl.kind != ResourceKind::Image) continue;

        r.isImage = true;
        const auto format = decl.imageDesc.format;
        // An undefined format on a non-imported image is resolved to a depth
        // format by the compiler, which passes the real aspect in itself.
        r.aspect = formatToAspectMask(format);
        if (decl.imported) continue;

        r.persistent = decl.imageDesc.persistent;
        r.shape.arrayLayers = decl.imageDesc.arrayLayers;
        if (decl.imageDesc.mipLevels != 0) {
            r.shape.mipLevels = decl.imageDesc.mipLevels;
        } else if (i < extents.size() && extents[i].width > 0 && extents[i].height > 0) {
            r.shape.mipLevels = fullMipChainLength(extents[i].width, extents[i].height);
        } else if (decl.imageDesc.width > 0 && decl.imageDesc.height > 0) {
            r.shape.mipLevels = fullMipChainLength(decl.imageDesc.width, decl.imageDesc.height);
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════
// Usage tables
// ═══════════════════════════════════════════════════════════════

VkImageLayout usageToLayout(ResourceUsage usage) {
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
        // The layout a pass renders INTO. PRESENT_SRC_KHR is where the image
        // goes afterwards, through a post-barrier; see ResourceAccess::present.
        case ResourceUsage::Present:                return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkPipelineStageFlags usageToStageMask(ResourceUsage usage, PassType passType) {
    switch (usage) {
        case ResourceUsage::ColorAttachmentWrite:
        case ResourceUsage::ColorAttachmentBlend:  // Blending uses same stage as color write
        case ResourceUsage::Present:
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
    }
    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}

VkAccessFlags usageToAccessMask(ResourceUsage usage) {
    switch (usage) {
        case ResourceUsage::ColorAttachmentWrite:
        case ResourceUsage::Present:
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
    }
    return 0;
}

bool readsPreviousContents(const ResourceAccess& access, bool isOutput) {
    if (!isOutput) return true;
    switch (access.usage) {
        case ResourceUsage::ColorAttachmentBlend:
        case ResourceUsage::DepthStencilReadOnly:
        case ResourceUsage::ShaderReadWrite:
            return true;
        case ResourceUsage::ColorAttachmentWrite:
        case ResourceUsage::DepthStencilWrite:
        case ResourceUsage::Present:
            return !access.hasClearValue;  // loaded rather than cleared
        default:
            return false;
    }
}

bool isAttachmentUsage(ResourceUsage usage) {
    switch (usage) {
        case ResourceUsage::ColorAttachmentWrite:
        case ResourceUsage::ColorAttachmentBlend:
        case ResourceUsage::Present:
        case ResourceUsage::DepthStencilWrite:
        case ResourceUsage::DepthStencilReadOnly:
            return true;
        default:
            return false;
    }
}

namespace {

constexpr VkAccessFlags kWriteAccess =
    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
    VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

/// A resolved range for `access` on `r`; buffers resolve to 1 x 1.
SubresourceRange resolvedRange(const ScheduledResource& r, const SubresourceRange& range) {
    return r.shape.resolve(range);
}

/// Flat index of a subresource in per-resource tables.
size_t subIndex(const ImageShape& shape, uint32_t mip, uint32_t layer) {
    return static_cast<size_t>(mip) * shape.arrayLayers + layer;
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// Validation
// ═══════════════════════════════════════════════════════════════

bool validateSubresources(const std::vector<PassDeclaration>& passes,
                          const std::vector<ResourceDeclaration>& declarations,
                          const std::vector<ScheduledResource>& resources,
                          const std::vector<DescriptorSetLayoutDesc>& descriptorLayouts,
                          std::string& outError)
{
    bool ok = true;
    auto fail = [&](const std::string& line) {
        outError += (outError.empty() ? "" : "\n") + line;
        ok = false;
    };

    auto checkRange = [&](const std::string& where, uint32_t ri, const SubresourceRange& range) -> bool {
        const auto& r = resources[ri];
        const auto resolved = resolvedRange(r, range);
        if (!r.shape.contains(resolved)) {
            fail(where + ": '" + declarations[ri].name + "' has " +
                 std::to_string(r.shape.mipLevels) + " mip level(s) and " +
                 std::to_string(r.shape.arrayLayers) + " layer(s); the requested range is outside it");
            return false;
        }
        return true;
    };

    for (const auto& pass : passes) {
        // subresource -> layout this pass wants it in
        std::map<std::tuple<uint32_t, uint32_t, uint32_t>, VkImageLayout> seen;

        auto checkAccess = [&](const ResourceAccess& access, bool isOutput) {
            if (!access.handle.valid() || access.handle.index >= resources.size()) return;
            const uint32_t ri = access.handle.index;
            const auto& r = resources[ri];
            const std::string where = "Pass '" + pass.name + "'";

            if (!r.isImage) {
                if (!access.subresource.isWholeImage()) {
                    fail(where + ": '" + declarations[ri].name + "' is a buffer and has no mips or layers");
                }
                return;
            }
            if (!checkRange(where, ri, access.subresource)) return;

            const auto resolved = resolvedRange(r, access.subresource);
            if (isOutput && isAttachmentUsage(access.usage) && resolved.mipCount != 1) {
                fail(where + ": attachment '" + declarations[ri].name +
                     "' must name one mip level (\"mip\": n)");
                return;
            }

            const VkImageLayout layout = usageToLayout(access.usage);
            for (uint32_t m = resolved.baseMip; m < resolved.baseMip + resolved.mipCount; ++m) {
                for (uint32_t l = resolved.baseLayer; l < resolved.baseLayer + resolved.layerCount; ++l) {
                    auto [it, inserted] = seen.emplace(std::make_tuple(ri, m, l), layout);
                    if (!inserted && it->second != layout) {
                        fail(where + " uses mip " + std::to_string(m) + ", layer " + std::to_string(l) +
                             " of '" + declarations[ri].name + "' in two different layouts");
                        return;
                    }
                }
            }
        };
        for (const auto& in : pass.inputs)   checkAccess(in, false);
        for (const auto& out : pass.outputs) checkAccess(out, true);
    }

    for (const auto& layout : descriptorLayouts) {
        for (const auto& binding : layout.bindings) {
            if (binding.autoBindResource.empty() || binding.autoBindSubresource.isWholeImage()) continue;
            for (uint32_t ri = 0; ri < declarations.size(); ++ri) {
                if (declarations[ri].name != binding.autoBindResource) continue;
                if (!resources[ri].isImage) {
                    fail("Binding '" + binding.name + "' of layout '" + layout.name +
                         "': '" + declarations[ri].name + "' is not an image");
                } else {
                    checkRange("Binding '" + binding.name + "' of layout '" + layout.name + "'",
                               ri, binding.autoBindSubresource);
                }
            }
        }
    }
    return ok;
}

// ═══════════════════════════════════════════════════════════════
// Dependencies
// ═══════════════════════════════════════════════════════════════

std::vector<std::vector<uint32_t>> passDependencies(
    const std::vector<PassDeclaration>& passes,
    const std::vector<ScheduledResource>& resources)
{
    const uint32_t passCount = static_cast<uint32_t>(passes.size());
    std::vector<std::vector<uint32_t>> deps(passCount);

    // resource -> (writer pass, resolved range) in declaration order
    std::vector<std::vector<std::pair<uint32_t, SubresourceRange>>> writers(resources.size());
    for (uint32_t pi = 0; pi < passCount; ++pi) {
        for (const auto& out : passes[pi].outputs) {
            if (!out.handle.valid() || out.handle.index >= resources.size()) continue;
            const auto& r = resources[out.handle.index];
            writers[out.handle.index].emplace_back(pi, resolvedRange(r, out.subresource));
        }
    }

    for (uint32_t pi = 0; pi < passCount; ++pi) {
        std::set<uint32_t> found;

        auto visit = [&](const ResourceAccess& access) {
            if (!access.handle.valid() || access.handle.index >= resources.size()) return;
            const uint32_t ri = access.handle.index;
            const auto& shape = resources[ri].shape;
            const auto want = resolvedRange(resources[ri], access.subresource);
            if (!shape.contains(want)) return;  // reported by validateSubresources

            // Subresources of `want` not yet attributed to a writer.
            std::vector<bool> open(static_cast<size_t>(shape.mipLevels) * shape.arrayLayers, false);
            size_t remaining = 0;
            for (uint32_t m = want.baseMip; m < want.baseMip + want.mipCount; ++m)
                for (uint32_t l = want.baseLayer; l < want.baseLayer + want.layerCount; ++l) {
                    open[subIndex(shape, m, l)] = true;
                    ++remaining;
                }

            const auto& ws = writers[ri];
            for (auto it = ws.rbegin(); it != ws.rend() && remaining > 0; ++it) {
                const auto& [writer, range] = *it;
                if (writer >= pi) continue;
                bool contributes = false;
                const uint32_t mEnd = std::min(range.baseMip + range.mipCount, shape.mipLevels);
                const uint32_t lEnd = std::min(range.baseLayer + range.layerCount, shape.arrayLayers);
                for (uint32_t m = range.baseMip; m < mEnd; ++m)
                    for (uint32_t l = range.baseLayer; l < lEnd; ++l) {
                        auto idx = subIndex(shape, m, l);
                        if (open[idx]) {
                            open[idx] = false;
                            --remaining;
                            contributes = true;
                        }
                    }
                if (contributes) found.insert(writer);
            }
        };

        for (const auto& in : passes[pi].inputs) visit(in);
        for (const auto& out : passes[pi].outputs) {
            if (readsPreviousContents(out, true)) visit(out);
        }
        deps[pi].assign(found.begin(), found.end());
    }
    return deps;
}

bool sortPasses(const std::vector<PassDeclaration>& passes,
                const std::vector<std::vector<uint32_t>>& dependencies,
                std::vector<uint32_t>& outOrder,
                std::string& outError)
{
    const uint32_t passCount = static_cast<uint32_t>(passes.size());
    std::vector<uint32_t> inDegree(passCount, 0);
    std::vector<std::vector<uint32_t>> dependents(passCount);
    for (uint32_t pi = 0; pi < passCount && pi < dependencies.size(); ++pi) {
        for (uint32_t dep : dependencies[pi]) {
            if (dep >= passCount) continue;
            dependents[dep].push_back(pi);
            ++inDegree[pi];
        }
    }

    // Min-heap: always the lowest ready declaration index.
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
    for (uint32_t pi = 0; pi < passCount; ++pi) {
        if (inDegree[pi] == 0) ready.push(pi);
    }

    outOrder.clear();
    outOrder.reserve(passCount);
    while (!ready.empty()) {
        const uint32_t pi = ready.top();
        ready.pop();
        outOrder.push_back(pi);
        for (uint32_t next : dependents[pi]) {
            if (--inDegree[next] == 0) ready.push(next);
        }
    }

    if (outOrder.size() != passCount) {
        outError = "FrameGraph: Cycle detected among passes: ";
        for (uint32_t pi = 0; pi < passCount; ++pi) {
            if (inDegree[pi] > 0) outError += "'" + passes[pi].name + "' ";
        }
        return false;
    }
    return true;
}

std::vector<uint32_t> livePasses(
    const std::vector<PassDeclaration>& passes,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<std::vector<uint32_t>>& dependencies)
{
    const uint32_t passCount = static_cast<uint32_t>(passes.size());
    std::vector<bool> live(passCount, false);
    std::queue<uint32_t> work;

    auto mark = [&](uint32_t pi) {
        if (!live[pi]) {
            live[pi] = true;
            work.push(pi);
        }
    };

    for (uint32_t pi = 0; pi < passCount; ++pi) {
        if (passes[pi].hasSideEffects) {
            mark(pi);
            continue;
        }
        for (const auto& out : passes[pi].outputs) {
            if (leavesPresentable(out) ||
                (out.handle.valid() && out.handle.index < declarations.size() &&
                 declarations[out.handle.index].imported)) {
                mark(pi);
            }
        }
    }

    while (!work.empty()) {
        const uint32_t pi = work.front();
        work.pop();
        if (pi < dependencies.size()) {
            for (uint32_t dep : dependencies[pi]) mark(dep);
        }
    }

    std::vector<uint32_t> out;
    for (uint32_t pi = 0; pi < passCount; ++pi) {
        if (live[pi]) out.push_back(pi);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════
// Views
// ═══════════════════════════════════════════════════════════════

VkImageViewType sampledViewType(ImageViewKind kind, const ImageShape& shape,
                                const SubresourceRange& resolved)
{
    const bool allLayers = resolved.baseLayer == 0 && resolved.layerCount == shape.arrayLayers;
    const bool cubeKind = kind == ImageViewKind::Cube || kind == ImageViewKind::CubeArray;

    if (allLayers) {
        switch (kind) {
            case ImageViewKind::Tex2D:      return VK_IMAGE_VIEW_TYPE_2D;
            case ImageViewKind::Tex2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            case ImageViewKind::Cube:       return VK_IMAGE_VIEW_TYPE_CUBE;
            case ImageViewKind::CubeArray:  return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            case ImageViewKind::Auto:
                return shape.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        }
    }
    if (kind == ImageViewKind::Tex2DArray) return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    if (cubeKind && resolved.baseLayer % 6 == 0 && resolved.layerCount % 6 == 0) {
        return resolved.layerCount == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return resolved.layerCount == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
}

VkImageViewType attachmentViewType(const SubresourceRange& resolved) {
    return resolved.layerCount == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
}

// ═══════════════════════════════════════════════════════════════
// Barrier planning
// ═══════════════════════════════════════════════════════════════

namespace {

struct SubState {
    VkImageLayout        layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags        access = 0;
    bool                 used   = false;  // accessed earlier this frame

    bool operator<(const SubState& o) const {
        return std::tie(layout, stage, access, used) < std::tie(o.layout, o.stage, o.access, o.used);
    }
    bool operator==(const SubState& o) const {
        return layout == o.layout && stage == o.stage && access == o.access && used == o.used;
    }
};

/// Transition the subresources of `range` on resource `ri` to `target`,
/// appending merged barriers to `out` and updating `states`.
void transition(std::vector<SubState>& states,
                const ScheduledResource& r,
                ResourceHandle handle,
                const SubresourceRange& range,
                VkImageLayout layout, VkPipelineStageFlags stage, VkAccessFlags access,
                std::vector<BarrierInfo>& out)
{
    const auto& shape = r.shape;
    const bool writes = (access & kWriteAccess) != 0;

    // (layerStart, layerCount, prior state) -> mips needing that barrier
    std::map<std::tuple<uint32_t, uint32_t, SubState>, std::vector<uint32_t>> runs;

    for (uint32_t m = range.baseMip; m < range.baseMip + range.mipCount; ++m) {
        uint32_t runStart = 0, runLen = 0;
        SubState runState{};
        auto flush = [&]() {
            if (runLen > 0) runs[{runStart, runLen, runState}].push_back(m);
            runLen = 0;
        };

        for (uint32_t l = range.baseLayer; l < range.baseLayer + range.layerCount; ++l) {
            auto& st = states[subIndex(shape, m, l)];
            const bool hazard = st.layout != layout ||
                                (st.access & kWriteAccess) != 0 ||
                                (writes && st.used);
            if (!hazard) {
                // Read after read in one layout: no barrier, but a later
                // writer must wait for this reader too.
                flush();
                st.stage |= stage;
                st.access |= access;
                st.used = true;
                continue;
            }
            if (runLen > 0 && runState == st && runStart + runLen == l) {
                ++runLen;
            } else {
                flush();
                runStart = l;
                runLen = 1;
                runState = st;
            }
        }
        flush();
    }

    for (const auto& [key, mips] : runs) {
        const auto& [layerStart, layerCount, prior] = key;
        // Split the mip list into consecutive blocks.
        size_t i = 0;
        while (i < mips.size()) {
            size_t j = i + 1;
            while (j < mips.size() && mips[j] == mips[j - 1] + 1) ++j;

            BarrierInfo b;
            b.resource  = handle;
            b.oldLayout = prior.layout;
            b.newLayout = layout;
            b.srcStage  = prior.used ? prior.stage : VkPipelineStageFlags{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
            b.srcAccess = prior.used ? (prior.access & kWriteAccess) : 0;
            b.dstStage  = stage;
            b.dstAccess = access;
            b.range.aspectMask     = r.aspect;
            b.range.baseMipLevel   = mips[i];
            b.range.levelCount     = static_cast<uint32_t>(j - i);
            b.range.baseArrayLayer = layerStart;
            b.range.layerCount     = layerCount;
            out.push_back(b);

            for (size_t k = i; k < j; ++k)
                for (uint32_t l = layerStart; l < layerStart + layerCount; ++l) {
                    auto& st = states[subIndex(shape, mips[k], l)];
                    st.layout = layout;
                    st.stage  = stage;
                    st.access = access;
                    st.used   = true;
                }
            i = j;
        }
    }
}

} // namespace

BarrierPlan planBarriers(
    const std::vector<PassDeclaration>& passes,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<ScheduledResource>& resources)
{
    BarrierPlan plan;
    plan.preBarriers.resize(passes.size());
    plan.postBarriers.resize(passes.size());
    plan.finalLayouts.resize(resources.size());

    std::vector<std::vector<SubState>> states(resources.size());
    for (size_t ri = 0; ri < resources.size(); ++ri) {
        if (resources[ri].isImage) {
            states[ri].resize(static_cast<size_t>(resources[ri].shape.mipLevels) *
                              resources[ri].shape.arrayLayers);
        }
    }

    // Which pass presents each presentable image: declared, or for the
    // swapchain with no declaration, its last writer in execution order.
    std::unordered_set<uint32_t> declaredPresent;
    std::vector<int64_t> lastWriter(resources.size(), -1);
    for (uint32_t pi : executionOrder) {
        for (const auto& out : passes[pi].outputs) {
            if (!out.handle.valid() || out.handle.index >= resources.size()) continue;
            if (leavesPresentable(out)) declaredPresent.insert(out.handle.index);
            lastWriter[out.handle.index] = pi;
        }
    }
    auto presentsAfter = [&](uint32_t pi, const ResourceAccess& out) {
        if (leavesPresentable(out)) return true;
        const uint32_t ri = out.handle.index;
        return resources[ri].presentFallback && !declaredPresent.count(ri) &&
               lastWriter[ri] == static_cast<int64_t>(pi);
    };
    for (size_t ri = 0; ri < resources.size(); ++ri) {
        if (resources[ri].presentFallback && !declaredPresent.count(static_cast<uint32_t>(ri)) &&
            lastWriter[ri] >= 0) {
            plan.warnings.push_back(
                "Pass '" + passes[static_cast<size_t>(lastWriter[ri])].name +
                "' is the last to write a presentable image but does not declare it. "
                "Add \"present\": true to that output; the compiler is assuming it for now.");
        }
    }

    // One frame of the graph: barriers into `pre`/`post`, states updated.
    auto runFrame = [&](std::vector<std::vector<BarrierInfo>>& pre,
                        std::vector<std::vector<BarrierInfo>>& post) {
        for (uint32_t pi : executionOrder) {
            const auto& pass = passes[pi];

            auto process = [&](const ResourceAccess& access) {
                if (!access.handle.valid() || access.handle.index >= resources.size()) return;
                const uint32_t ri = access.handle.index;
                const auto& r = resources[ri];
                if (!r.isImage) return;
                const auto range = resolvedRange(r, access.subresource);
                if (!r.shape.contains(range)) return;  // reported by validateSubresources

                transition(states[ri], r, access.handle, range,
                           usageToLayout(access.usage),
                           usageToStageMask(access.usage, pass.type),
                           usageToAccessMask(access.usage),
                           pre[pi]);
            };
            for (const auto& in : pass.inputs)   process(in);
            for (const auto& out : pass.outputs) process(out);

            for (const auto& out : pass.outputs) {
                if (!out.handle.valid() || out.handle.index >= resources.size()) continue;
                const uint32_t ri = out.handle.index;
                const auto& r = resources[ri];
                if (!r.isImage || !presentsAfter(pi, out)) continue;
                const auto range = resolvedRange(r, out.subresource);
                if (!r.shape.contains(range)) continue;

                transition(states[ri], r, out.handle, range,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                           post[pi]);
            }
        }
    };

    // The graph runs every frame, and with several frames in flight one
    // frame's first access to an image can overlap the previous frame's last.
    // Plan a frame once to find where each subresource ends up, then plan it
    // for real starting from there, so each first barrier waits for the
    // previous frame's last access. The layout still starts UNDEFINED: the
    // first frame has nothing to wait for and no contents to keep. Persistent
    // images keep theirs, and are put in those layouts once at compile time.
    {
        std::vector<std::vector<BarrierInfo>> scratchPre(passes.size()), scratchPost(passes.size());
        runFrame(scratchPre, scratchPost);
    }
    for (size_t ri = 0; ri < resources.size(); ++ri) {
        for (auto& st : states[ri]) {
            if (!st.used) continue;
            if (st.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                // Presentation is ordered by the acquire semaphore, which the
                // frame's submission waits on at COLOR_ATTACHMENT_OUTPUT; the
                // transition joins that dependency chain.
                st.stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                st.access = 0;
            }
            if (!resources[ri].persistent) st.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }
    runFrame(plan.preBarriers, plan.postBarriers);

    for (size_t ri = 0; ri < resources.size(); ++ri) {
        plan.finalLayouts[ri].reserve(states[ri].size());
        for (const auto& st : states[ri]) plan.finalLayouts[ri].push_back(st.layout);
    }
    return plan;
}

} // namespace FrameGraph
} // namespace Shoonyakasha
