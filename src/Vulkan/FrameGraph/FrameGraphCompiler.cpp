//
// Shoonyakasha Engine - Frame Graph Compiler
//
// 玄武司察  深潛而見真
// The Dark Warrior governs investigation — diving deep to see truth
//

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "GPU/VkFormatUtils.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanRayTracingPipeline.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanMemoryAllocator.h"
#include "Vulkan/RayTracingScene.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "FrameGraph/InstanceBatching.h"
#include "FrameGraph/ShaderInterfaceValidator.h"
#include "Core/Logger.h"

#include <fstream>
#include <map>
#include <queue>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cassert>
#include <cstring>

namespace Shoonyakasha {
namespace FrameGraph {

FrameGraphCompiler::~FrameGraphCompiler() {
    delete m_logger;
}

ExecutionKind executionKindOf(const std::string& type) {
    if (type == "none")             return ExecutionKind::None;
    if (type == "fullscreen")       return ExecutionKind::Fullscreen;
    if (type == "draw")             return ExecutionKind::Draw;
    if (type == "compute_dispatch") return ExecutionKind::ComputeDispatch;
    if (type == "compute_image")    return ExecutionKind::ComputeImage;
    if (type == "dispatch_indirect") return ExecutionKind::DispatchIndirect;
    if (type == "draw_indirect")    return ExecutionKind::DrawIndirect;
    if (type == "draw_indexed")     return ExecutionKind::DrawIndexed;
    if (type == "draw_indexed_indirect") return ExecutionKind::DrawIndexedIndirect;
    if (type == "copy")             return ExecutionKind::Copy;
    if (type == "blit")             return ExecutionKind::Blit;
    if (type == "draw_mesh_tasks")  return ExecutionKind::DrawMeshTasks;
    if (type == "trace_rays")       return ExecutionKind::TraceRays;
    if (type == "manual" || type.empty()) return ExecutionKind::Manual;
    if (type == "scene_geometry" || type == "ui_canvas" || isEntityGeometryExecutionType(type))
        return ExecutionKind::SceneRenderer;
    return ExecutionKind::Unknown;
}

bool compileTransferRegions(const PassDeclaration& pass, const std::vector<PhysicalResource>& resources,
                            const std::vector<ResourceDeclaration>& declarations,
                            CompiledPass& compiled, std::string& outError)
{
    const std::string where = "Pass '" + pass.name + "'";
    const auto& src = pass.inputs.at(0);
    const auto& dst = pass.outputs.at(0);
    const auto* srcImg = std::get_if<PhysicalImage>(&resources.at(src.handle.index));
    const auto* dstImg = std::get_if<PhysicalImage>(&resources.at(dst.handle.index));
    if (!srcImg || !dstImg) {
        outError = where + ": copy and blit take images, not buffers";
        return false;
    }
    const std::string& srcName = declarations[src.handle.index].name;
    const std::string& dstName = declarations[dst.handle.index].name;
    for (const auto* decl : {&declarations[src.handle.index], &declarations[dst.handle.index]}) {
        if (decl->imported) {
            outError = where + ": '" + decl->name + "' is imported; copy and blit take images the graph creates";
            return false;
        }
    }
    const auto s = srcImg->shape.resolve(src.subresource);
    const auto d = dstImg->shape.resolve(dst.subresource);
    if (s.mipCount != d.mipCount || s.layerCount != d.layerCount) {
        outError = where + ": '" + srcName + "' gives " + std::to_string(s.mipCount) + " mip(s) and " +
                   std::to_string(s.layerCount) + " layer(s), but '" + dstName + "' takes " +
                   std::to_string(d.mipCount) + " and " + std::to_string(d.layerCount);
        return false;
    }
    if (srcImg->aspect != dstImg->aspect) {
        outError = where + ": '" + srcName + "' and '" + dstName + "' have different aspects (colour, depth, stencil)";
        return false;
    }

    const bool blit = compiled.executionKind == ExecutionKind::Blit;
    if (blit && (srcImg->samples != VK_SAMPLE_COUNT_1_BIT || dstImg->samples != VK_SAMPLE_COUNT_1_BIT)) {
        outError = where + ": a blit takes single-sampled images; resolve '" + srcName + "' first";
        return false;
    }
    if (srcImg->samples != dstImg->samples) {
        outError = where + ": a copy needs '" + srcName + "' and '" + dstName + "' with one sample count";
        return false;
    }
    if (!blit && srcImg->format != dstImg->format) {
        outError = where + ": a copy needs '" + srcName + "' and '" + dstName + "' in one format; use blit";
        return false;
    }

    compiled.transferSrc = src.handle;
    compiled.transferDst = dst.handle;
    compiled.copyRegions.clear();
    compiled.blitRegions.clear();
    compiled.blitFilter = pass.execution.filter == "nearest" ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

    for (uint32_t i = 0; i < s.mipCount; ++i) {
        const uint32_t sm = s.baseMip + i, dm = d.baseMip + i;
        const VkExtent2D se = srcImg->mipExtent(sm), de = dstImg->mipExtent(dm);
        const uint32_t sd = srcImg->mipDepth(sm), dd = dstImg->mipDepth(dm);
        const VkImageSubresourceLayers sl{srcImg->aspect, sm, s.baseLayer, s.layerCount};
        const VkImageSubresourceLayers dl{dstImg->aspect, dm, d.baseLayer, d.layerCount};
        if (blit) {
            VkImageBlit region{};
            region.srcSubresource = sl;
            region.srcOffsets[1] = {static_cast<int32_t>(se.width), static_cast<int32_t>(se.height),
                                    static_cast<int32_t>(sd)};
            region.dstSubresource = dl;
            region.dstOffsets[1] = {static_cast<int32_t>(de.width), static_cast<int32_t>(de.height),
                                    static_cast<int32_t>(dd)};
            compiled.blitRegions.push_back(region);
        } else {
            if (se.width != de.width || se.height != de.height || sd != dd) {
                outError = where + ": mip " + std::to_string(sm) + " of '" + srcName + "' is " +
                           std::to_string(se.width) + "x" + std::to_string(se.height) + "x" + std::to_string(sd) +
                           ", mip " + std::to_string(dm) + " of '" + dstName + "' is " +
                           std::to_string(de.width) + "x" + std::to_string(de.height) + "x" + std::to_string(dd) +
                           "; a copy needs equal sizes; use blit";
                return false;
            }
            VkImageCopy region{};
            region.srcSubresource = sl;
            region.dstSubresource = dl;
            region.extent = {se.width, se.height, sd};
            compiled.copyRegions.push_back(region);
        }
    }
    return true;
}

std::vector<VkPipelineColorBlendAttachmentState> colorBlendStates(const PassDeclaration& pass) {
    const auto& pd = pass.pipelineDesc;
    BlendDesc shared;
    shared.blending       = pd.blending;
    shared.srcColorFactor = pd.srcColorBlendFactor;
    shared.dstColorFactor = pd.dstColorBlendFactor;
    shared.colorBlendOp   = pd.colorBlendOp;
    shared.srcAlphaFactor = pd.srcAlphaBlendFactor;
    shared.dstAlphaFactor = pd.dstAlphaBlendFactor;
    shared.alphaBlendOp   = pd.alphaBlendOp;

    auto state = [](const BlendDesc& b, VkColorComponentFlags mask) {
        VkPipelineColorBlendAttachmentState s{};
        s.colorWriteMask      = mask;
        s.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        s.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        s.colorBlendOp        = VK_BLEND_OP_ADD;
        s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        s.alphaBlendOp        = VK_BLEND_OP_ADD;
        if (b.blending == "alpha") {
            s.blendEnable = VK_TRUE;
            s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        } else if (b.blending == "additive") {
            s.blendEnable = VK_TRUE;
            s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        } else if (b.blending == "custom") {
            s.blendEnable = VK_TRUE;
            s.srcColorBlendFactor = JsonUtils::stringToBlendFactor(b.srcColorFactor);
            s.dstColorBlendFactor = JsonUtils::stringToBlendFactor(b.dstColorFactor);
            s.colorBlendOp        = JsonUtils::stringToBlendOp(b.colorBlendOp);
            s.srcAlphaBlendFactor = JsonUtils::stringToBlendFactor(b.srcAlphaFactor);
            s.dstAlphaBlendFactor = JsonUtils::stringToBlendFactor(b.dstAlphaFactor);
            s.alphaBlendOp        = JsonUtils::stringToBlendOp(b.alphaBlendOp);
        }
        return s;
    };

    std::vector<VkPipelineColorBlendAttachmentState> out;
    for (const auto& output : pass.outputs) {
        const bool colour = output.usage == ResourceUsage::ColorAttachmentWrite ||
                            output.usage == ResourceUsage::ColorAttachmentBlend ||
                            output.usage == ResourceUsage::Present;
        if (!colour || !output.handle.valid()) continue;
        out.push_back(state(output.blend ? *output.blend : shared,
                            output.writeMask ? *output.writeMask : pd.writeMask));
    }
    return out;
}

std::string resolveTargetError(const PhysicalImage& source, const SubresourceRange& sourceRange,
                               const std::string& sourceName, const PhysicalImage& target,
                               const std::string& targetName) {
    const std::string s = "'" + sourceName + "'", t = "'" + targetName + "'";
    if (source.samples == VK_SAMPLE_COUNT_1_BIT) return s + " resolves into " + t + " but is not multisampled";
    if (target.samples != VK_SAMPLE_COUNT_1_BIT) return "resolve target " + t + " is multisampled";
    if (target.format != source.format) return s + " and its resolve target " + t + " differ in format";
    if (target.aspect != VK_IMAGE_ASPECT_COLOR_BIT) return "resolve target " + t + " is not a colour image";
    const VkExtent2D se = source.mipExtent(sourceRange.baseMip);
    if (target.shape.mipLevels != 1 || target.shape.arrayLayers != sourceRange.layerCount ||
        target.extent.width != se.width || target.extent.height != se.height) {
        return "resolve target " + t + " must be one mip of " + std::to_string(se.width) + "x" +
               std::to_string(se.height) + " with " + std::to_string(sourceRange.layerCount) +
               " layer(s), as " + s + " is rendered";
    }
    return {};
}

std::string stencilAttachmentError(const PassDeclaration& pass, const CompiledPass& compiled) {
    const auto& pd = pass.pipelineDesc;
    if (!pd.stencilTest) return {};
    const std::string where = "Pass '" + pass.name + "'";
    if (!compiled.hasDepthAttachment || !isStencilFormat(compiled.depthAttachment.format)) {
        return where + " has a stencil test but no depth attachment with a stencil format "
                       "(D16_UNORM_S8_UINT, D24_UNORM_S8_UINT or D32_SFLOAT_S8_UINT)";
    }
    auto writes = [](const VkStencilOpState& s) {
        return s.writeMask != 0 && (s.failOp != VK_STENCIL_OP_KEEP || s.passOp != VK_STENCIL_OP_KEEP ||
                                    s.depthFailOp != VK_STENCIL_OP_KEEP);
    };
    if ((writes(pd.stencilFront) || writes(pd.stencilBack)) &&
        compiled.depthAttachment.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        return where + " writes stencil into a depth_read attachment; use depth_write";
    }
    return {};
}

std::string sceneBuffersError(const std::vector<BufferLayoutDesc>& layouts,
                              const std::unordered_map<std::string, CompiledBufferLayout>& compiled) {
    for (const auto& desc : layouts) {
        // Component rows are whatever the application's rows are.
        if (desc.initConfig.type != "scene" || desc.initConfig.sceneData == "components") continue;
        const auto it = compiled.find(desc.name);
        if (it == compiled.end()) continue;
        const auto& fields = it->second.fields;
        if (desc.initConfig.sceneData == "rayInstances") {
            using T = BufferFieldType;
            const T expected[8] = {T::UVec4, T::UVec4, T::Vec4, T::Vec4, T::Vec4, T::UVec4, T::Vec4, T::Vec4};
            bool matches = fields.size() == 8 && it->second.totalSize == sizeof(PackedRayInstance);
            for (size_t i = 0; matches && i < 8; ++i) {
                matches = fields[i].type == expected[i] && fields[i].arrayCount == 1;
            }
            if (!matches) {
                return "Buffer layout '" + desc.name + "': ray instances are the fields geometry (uvec4), "
                       "vertexLayout (uvec4), baseColor, emissive, surface (vec4), textures (uvec4), optics "
                       "and film (vec4), " + std::to_string(sizeof(PackedRayInstance)) + " bytes each";
            }
            continue;
        }
        const bool sixVec4 = fields.size() == 6 &&
            std::all_of(fields.begin(), fields.end(), [](const BufferFieldDesc& f) {
                return f.type == BufferFieldType::Vec4 && f.arrayCount == 1;
            });
        if (!sixVec4 || it->second.totalSize != sizeof(SceneContext::PackedLight)) {
            return "Buffer layout '" + desc.name + "': scene lights are six vec4 fields (positionType, "
                   "colorIntensity, directionRange, attenuation, source, shape), " +
                   std::to_string(sizeof(SceneContext::PackedLight)) + " bytes each";
        }
    }
    return "";
}

std::string indirectArgumentsError(const std::vector<PassDeclaration>& passes,
                                   const std::vector<BufferLayoutDesc>& layouts,
                                   const std::unordered_map<std::string, CompiledBufferLayout>& compiled) {
    auto check = [&](const PassDeclaration& pass, const std::string& name, uint64_t needed) -> std::string {
        const auto desc = std::find_if(layouts.begin(), layouts.end(),
                                       [&](const BufferLayoutDesc& d) { return d.name == name; });
        if (desc == layouts.end()) return "";
        const auto it = compiled.find(name);
        if (it == compiled.end() || it->second.usage != BufferUsageType::StorageBuffer) {
            return "Pass '" + pass.name + "' reads '" + name + "', which is not a storage buffer";
        }
        const uint64_t size = static_cast<uint64_t>(it->second.totalSize) * std::max(desc->elementCount, 1u);
        if (size < needed) {
            return "Pass '" + pass.name + "': '" + name + "' holds " + std::to_string(size) +
                   " bytes; the pass reads " + std::to_string(needed);
        }
        return "";
    };
    for (const auto& pass : passes) {
        const auto& exec = pass.execution;
        std::string error;
        if (exec.type == "dispatch_indirect") {
            error = check(pass, exec.argumentBuffer, uint64_t{exec.argumentOffset} + 12);
        } else if (exec.type == "draw_indirect" || exec.type == "draw_indexed_indirect") {
            const uint32_t draws = exec.countBuffer.empty() ? exec.drawCount : exec.maxDrawCount;
            const uint64_t commandSize = exec.type == "draw_indexed_indirect" ? 20 : 16;
            error = check(pass, exec.argumentBuffer,
                          uint64_t{exec.argumentOffset} + uint64_t{std::max(draws, 1u) - 1} * exec.drawStride +
                              commandSize);
            if (error.empty() && !exec.countBuffer.empty()) {
                error = check(pass, exec.countBuffer, uint64_t{exec.countOffset} + 4);
            }
        }
        // Vertex and index buffers: storage buffers that reach past their offset.
        if (error.empty() && !exec.vertexBuffer.empty()) {
            error = check(pass, exec.vertexBuffer, uint64_t{exec.vertexBufferOffset} + 1);
        }
        if (error.empty() && !exec.indexBuffer.empty()) {
            error = check(pass, exec.indexBuffer, uint64_t{exec.indexBufferOffset} + 1);
        }
        if (!error.empty()) return error;
    }
    return "";
}

// ═══════════════════════════════════════════════════════════════
// Usage → Vulkan mapping helpers
// ═══════════════════════════════════════════════════════════════

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
        case ResourceUsage::ColorResolve:
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// Physical Resource Creation
// ═══════════════════════════════════════════════════════════════

bool FrameGraphCompiler::createPhysicalResources(
    VulkanDevice& device,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<PassDeclaration>& passes,
    std::vector<PhysicalResource>& outResources,
    VkExtent2D referenceExtent,
    const ImportedImageMap& importedImages,
    const std::vector<bool>& sharedWithCompute,
    std::string& outError)
{
    outResources.resize(declarations.size());
    m_deferredImages.clear();

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
                physImg.aspect = formatToAspectMask(physImg.format);
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
            width  = std::max(width, 1u);
            height = std::max(height, 1u);

            // Mips and layers
            const bool volume = desc.viewType == ImageViewKind::Tex3D;
            const uint32_t depth = volume ? desc.depth : 1u;
            const uint32_t maxMips = fullMipChainLength(width, height, depth);
            const uint32_t mipLevels = desc.mipLevels == 0 ? maxMips : desc.mipLevels;
            if (mipLevels > maxMips) {
                outError = "Resource '" + decl.name + "' asks for " + std::to_string(mipLevels) +
                           " mip levels; " + std::to_string(width) + "x" + std::to_string(height) +
                           (volume ? "x" + std::to_string(depth) : "") +
                           " has at most " + std::to_string(maxMips);
                return false;
            }
            const bool cube = desc.viewType == ImageViewKind::Cube ||
                              desc.viewType == ImageViewKind::CubeArray;
            if (cube && width != height) {
                outError = "Resource '" + decl.name + "' is a cube image but is " +
                           std::to_string(width) + "x" + std::to_string(height) + "; faces must be square";
                return false;
            }
            const ImageShape shape{mipLevels, desc.arrayLayers};

            // Combine declared additional usage with usage derived from passes
            VkImageUsageFlags usage = imageUsages[i] | desc.additionalUsage;

            // Readback and save copy out of the image, and no pass declares
            // that, so TRANSFER_SRC is added from the policy instead. SSBOs get
            // the equivalent buffer usage in RenderGraph::createSSBOs. Without
            // it vkCmdCopyImageToBuffer is rejected.
            if (decl.readbackPolicy.enabled || decl.savePolicy.enabled) {
                usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }

            // Determine memory properties based on usage
            VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

            // Determine if this is a depth resource from usage flags or format
            bool isDepthByUsage = (imageUsages[i] & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            bool isDepth = isDepthByUsage || Shoonyakasha::isDepthFormat(desc.format);

            VkFormat format = desc.format;
            if (format == VK_FORMAT_UNDEFINED && isDepth) {
                // Auto-select best available depth format
                format = device.findDepthFormat();
            }

            // A sample count the device does not offer for this format and
            // usage fails here rather than at image creation.
            if (desc.samples != VK_SAMPLE_COUNT_1_BIT) {
                VkImageFormatProperties props{};
                const VkResult supported = vkGetPhysicalDeviceImageFormatProperties(
                    device.getPhysicalDevice(), format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &props);
                if (supported != VK_SUCCESS || (props.sampleCounts & desc.samples) == 0) {
                    outError = "Resource '" + decl.name + "': this device has no " +
                               std::to_string(static_cast<int>(desc.samples)) +
                               "-sample images of its format and usage";
                    return false;
                }
            }

            VkImageCreateInfo info = VulkanImage::createInfo(
                width, height, format, VK_IMAGE_TILING_OPTIMAL, usage, shape.mipLevels, shape.arrayLayers,
                cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
                volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D, depth, desc.samples);
            const bool shared = i < sharedWithCompute.size() && sharedWithCompute[i];
            if (shared) device.shareWithCompute(info);

            // The whole-image view shaders sample. A sampled depth/stencil view
            // may name only one aspect, so depth formats are viewed as depth.
            const VkImageAspectFlags fullAspect = Shoonyakasha::formatToAspectMask(format);
            const VkImageAspectFlags sampledAspect =
                Shoonyakasha::isDepthFormat(format) ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT} : fullAspect;
            SubresourceRange whole;
            const VkImageViewType viewType = sampledViewType(desc.viewType, shape, shape.resolve(whole));

            PhysicalImage physImg;
            physImg.format = format;
            physImg.extent = {width, height};
            physImg.depth = depth;
            physImg.samples = desc.samples;
            physImg.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            physImg.shape = shape;
            physImg.viewType = viewType;
            physImg.aspect = fullAspect;
            physImg.subresourceViews = std::make_unique<ImageViewSet>(device.getLogicalDevice());
            outResources[i] = std::move(physImg);

            // A transient image is created once the passes are ordered, in
            // memory it may share (createTransientImages).
            const DeferredImage deferred{i, info, sampledAspect, viewType};
            if (desc.transient) {
                m_deferredImages.push_back(deferred);
            } else {
                finishImage(std::get<PhysicalImage>(outResources[i]), deferred,
                            std::make_unique<VulkanImage>(
                                device, width, height, format, VK_IMAGE_TILING_OPTIMAL, usage, memProps,
                                shape.mipLevels, shape.arrayLayers, info.flags, info.imageType, depth,
                                desc.samples, shared));
            }
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
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Transient images and the memory they share
// ═══════════════════════════════════════════════════════════════

struct SharedImageMemory {
    VmaAllocator  allocator  = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    ~SharedImageMemory() {
        if (allocation != VK_NULL_HANDLE) vmaFreeMemory(allocator, allocation);
    }
};

void FrameGraphCompiler::finishImage(PhysicalImage& physImg, const DeferredImage& deferred,
                                     std::unique_ptr<VulkanImage> image) {
    // An image only copied to and from has no view.
    constexpr VkImageUsageFlags kViewUsages =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if (deferred.info.usage & kViewUsages) image->createImageView(deferred.sampledAspect, deferred.viewType);
    physImg.vkImage = image->getImage();
    physImg.view = image->getImageView();
    physImg.ownedImage = std::move(image);
}

bool FrameGraphCompiler::createTransientImages(
    VulkanDevice& device,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<PassDeclaration>& passes,
    const std::vector<DescriptorSetLayoutDesc>& descriptorLayouts,
    const std::vector<uint32_t>& executionOrder,
    std::vector<PhysicalResource>& physResources,
    std::vector<ScheduledResource>& scheduled,
    std::string& outError)
{
    if (m_deferredImages.empty()) return true;

    const auto aliasable = aliasableImages(passes, declarations, descriptorLayouts, executionOrder);
    const auto lifetimes = resourceLifetimes(passes, declarations, descriptorLayouts, executionOrder);

    std::vector<AliasRequest> requests;
    std::unordered_map<uint32_t, const DeferredImage*> byResource;
    for (const auto& deferred : m_deferredImages) {
        auto& physImg = std::get<PhysicalImage>(physResources[deferred.resource]);
        const auto& info = deferred.info;
        if (!aliasable[deferred.resource]) {
            m_logger->log(LogLevel::Info, "  Transient image '%s' cannot share memory; it gets its own",
                          declarations[deferred.resource].name.c_str());
            finishImage(physImg, deferred, std::make_unique<VulkanImage>(
                device, info.extent.width, info.extent.height, info.format, info.tiling, info.usage,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, info.mipLevels, info.arrayLayers, info.flags,
                info.imageType, info.extent.depth, info.samples, info.sharingMode == VK_SHARING_MODE_CONCURRENT));
            continue;
        }
        VkDeviceImageMemoryRequirements query{};
        query.sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS;
        query.pCreateInfo = &info;
        VkMemoryRequirements2 requirements{};
        requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        vkGetDeviceImageMemoryRequirements(device.getLogicalDevice(), &query, &requirements);
        const auto& r = requirements.memoryRequirements;
        requests.push_back({deferred.resource, r.size, r.alignment, r.memoryTypeBits});
        byResource[deferred.resource] = &deferred;
    }

    const auto blocks = packAliases(requests, lifetimes);
    VkDeviceSize separate = 0, shared = 0;
    for (const auto& request : requests) separate += request.size;

    for (uint32_t b = 0; b < blocks.size(); ++b) {
        const auto& block = blocks[b];
        auto memory = std::make_shared<SharedImageMemory>();
        memory->allocator = device.getAllocator().getHandle();
        const VkMemoryRequirements requirements{block.size, block.alignment, block.memoryTypeBits};
        VmaAllocationCreateInfo create{};
        create.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (vmaAllocateMemory(memory->allocator, &requirements, &create, &memory->allocation, nullptr) != VK_SUCCESS) {
            outError = "Could not allocate " + std::to_string(block.size) + " bytes for transient images";
            return false;
        }
        shared += block.size;

        std::string names;
        for (uint32_t ri : block.resources) {
            auto& physImg = std::get<PhysicalImage>(physResources[ri]);
            const auto& deferred = *byResource.at(ri);
            finishImage(physImg, deferred,
                        std::make_unique<VulkanImage>(device, deferred.info, memory->allocation, 0));
            physImg.sharedMemory = memory;
            if (block.resources.size() > 1) scheduled[ri].aliasBlock = b;
            names += (names.empty() ? "" : ", ") + declarations[ri].name;
        }
        m_logger->log(LogLevel::Info, "  Memory block %u (%.1f MiB): %s", b,
                      static_cast<double>(block.size) / (1024.0 * 1024.0), names.c_str());
    }
    m_logger->log(LogLevel::Info, "Transient images: %zu in %zu memory block(s), %.1f MiB instead of %.1f MiB",
                  requests.size(), blocks.size(), static_cast<double>(shared) / (1024.0 * 1024.0),
                  static_cast<double>(separate) / (1024.0 * 1024.0));
    m_deferredImages.clear();
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Subresource views
// ═══════════════════════════════════════════════════════════════

ImageViewSet::~ImageViewSet() {
    for (const auto& e : m_entries) {
        vkDestroyImageView(m_device, e.view, nullptr);
    }
}

VkImageView ImageViewSet::get(VkImage image, VkFormat format, VkImageViewType type,
                              const VkImageSubresourceRange& range) {
    for (const auto& e : m_entries) {
        if (e.type == type &&
            e.range.aspectMask == range.aspectMask &&
            e.range.baseMipLevel == range.baseMipLevel &&
            e.range.levelCount == range.levelCount &&
            e.range.baseArrayLayer == range.baseArrayLayer &&
            e.range.layerCount == range.layerCount) {
            return e.view;
        }
    }

    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = type;
    info.format = format;
    info.subresourceRange = range;

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(m_device, &info, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create a subresource image view");
    }
    m_entries.push_back({type, range, view});
    return view;
}

// ═══════════════════════════════════════════════════════════════
// Attachments — what each pass renders into
// ═══════════════════════════════════════════════════════════════

bool FrameGraphCompiler::createAttachments(
    std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    std::vector<PhysicalResource>& physResources,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<std::vector<AttachmentOps>>& attachmentOps,
    std::string& outError)
{
    for (uint32_t execIdx : executionOrder) {
        auto& compiled = compiledPasses[execIdx];
        const auto& passDecl = passes[compiled.declIndex];

        compiled.colorAttachments.clear();
        compiled.hasDepthAttachment = false;
        compiled.extent = {};
        compiled.extentDepth = 1;
        compiled.layerCount = 1;
        compiled.samples = VK_SAMPLE_COUNT_1_BIT;
        bool haveExtent = false;
        bool haveSamples = false;

        // Compute passes render nothing; their extent, which compute_image
        // dispatches over, is that of the first image they write at the mip
        // level they write, or failing that the first image they read.
        if (passDecl.type != PassType::Graphics) {
            for (const auto* list : {&passDecl.outputs, &passDecl.inputs}) {
                for (const auto& access : *list) {
                    if (haveExtent || !access.handle.valid()) continue;
                    const auto* physImg = std::get_if<PhysicalImage>(&physResources[access.handle.index]);
                    if (!physImg) continue;
                    const uint32_t mip = physImg->shape.resolve(access.subresource).baseMip;
                    compiled.extent = physImg->mipExtent(mip);
                    compiled.extentDepth = physImg->mipDepth(mip);
                    haveExtent = true;
                }
            }
            continue;
        }

        for (size_t oi = 0; oi < passDecl.outputs.size(); ++oi) {
            const auto& output = passDecl.outputs[oi];
            if (!output.handle.valid() || !isAttachmentUsage(output.usage)) continue;
            const AttachmentOps ops = compiled.declIndex < attachmentOps.size() &&
                                      oi < attachmentOps[compiled.declIndex].size()
                                    ? attachmentOps[compiled.declIndex][oi] : AttachmentOps{};
            const uint32_t ri = output.handle.index;
            auto* physImg = std::get_if<PhysicalImage>(&physResources[ri]);
            if (!physImg) continue;

            const auto range = physImg->shape.resolve(output.subresource);
            const bool isDepth = output.usage == ResourceUsage::DepthStencilWrite ||
                                 output.usage == ResourceUsage::DepthStencilReadOnly;

            CompiledAttachment att;
            att.resource = output.handle;
            att.format   = physImg->format;
            att.layout   = usageToLayout(output.usage);
            // Loads of contents nothing wrote this frame, and stores nothing
            // reads, cost bandwidth for nothing; the plan finds both.
            att.loadOp   = output.hasClearValue ? VK_ATTACHMENT_LOAD_OP_CLEAR
                         : ops.discardLoad      ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                                : VK_ATTACHMENT_LOAD_OP_LOAD;
            // A read-only depth attachment must keep its contents for later
            // readers; STORE_OP_NONE leaves them untouched without writing.
            att.storeOp  = output.usage == ResourceUsage::DepthStencilReadOnly ? VK_ATTACHMENT_STORE_OP_NONE
                         : ops.discardStore ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                            : VK_ATTACHMENT_STORE_OP_STORE;
            if (output.hasClearValue) {
                att.clearValue = output.clearValue;
            } else if (isDepth) {
                att.clearValue.depthStencil = {1.0f, 0};
            } else {
                att.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            }

            if (!declarations[ri].imported) {
                // The whole-image view of a depth/stencil format shows depth
                // only; as an attachment it needs both aspects.
                const bool wholeSingle = physImg->shape.mipLevels == 1 && physImg->shape.arrayLayers == 1 &&
                                         physImg->viewType == VK_IMAGE_VIEW_TYPE_2D &&
                                         !isStencilFormat(physImg->format);
                if (wholeSingle) {
                    att.view = physImg->view;
                } else {
                    VkImageSubresourceRange vr{};
                    vr.aspectMask     = physImg->aspect;
                    vr.baseMipLevel   = range.baseMip;
                    vr.levelCount     = 1;
                    vr.baseArrayLayer = range.baseLayer;
                    vr.layerCount     = range.layerCount;
                    att.view = physImg->subresourceViews->get(
                        physImg->vkImage, physImg->format, attachmentViewType(range), vr);
                }
            }

            // Every attachment of a pass shares one render area and layer count.
            const VkExtent2D ext = physImg->mipExtent(range.baseMip);
            if (!haveExtent) {
                compiled.extent = ext;
                compiled.layerCount = range.layerCount;
                haveExtent = true;
            } else if (ext.width != compiled.extent.width || ext.height != compiled.extent.height ||
                       range.layerCount != compiled.layerCount) {
                outError = "Pass '" + passDecl.name + "': attachment '" + declarations[ri].name + "' is " +
                           std::to_string(ext.width) + "x" + std::to_string(ext.height) + " with " +
                           std::to_string(range.layerCount) + " layer(s), but the pass's other attachments are " +
                           std::to_string(compiled.extent.width) + "x" + std::to_string(compiled.extent.height) +
                           " with " + std::to_string(compiled.layerCount);
                return false;
            }

            // Every attachment of a pass has one sample count.
            if (!haveSamples) {
                compiled.samples = physImg->samples;
                haveSamples = true;
            } else if (physImg->samples != compiled.samples) {
                outError = "Pass '" + passDecl.name + "': attachment '" + declarations[ri].name + "' has " +
                           std::to_string(static_cast<int>(physImg->samples)) + " sample(s), but the pass's " +
                           "other attachments have " + std::to_string(static_cast<int>(compiled.samples));
                return false;
            }

            if (output.resolveTarget.valid()) {
                const uint32_t ti = output.resolveTarget.index;
                const auto* target = std::get_if<PhysicalImage>(&physResources[ti]);
                const std::string error = target
                    ? resolveTargetError(*physImg, range, declarations[ri].name, *target, declarations[ti].name)
                    : "'" + declarations[ti].name + "' is not an image";
                if (!error.empty()) {
                    outError = "Pass '" + passDecl.name + "': " + error;
                    return false;
                }
                att.resolveResource = output.resolveTarget;
                // Imported targets take this frame's view when recording.
                if (!declarations[ti].imported) {
                    const bool wholeSingle = target->shape.mipLevels == 1 && target->shape.arrayLayers == 1 &&
                                             target->viewType == VK_IMAGE_VIEW_TYPE_2D;
                    if (wholeSingle) {
                        att.resolveView = target->view;
                    } else {
                        const VkImageSubresourceRange vr{target->aspect, 0, 1, 0, target->shape.arrayLayers};
                        att.resolveView = target->subresourceViews->get(
                            target->vkImage, target->format,
                            target->shape.arrayLayers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY, vr);
                    }
                }
            }

            if (isDepth) {
                if (compiled.hasDepthAttachment) {
                    outError = "Pass '" + passDecl.name + "' has more than one depth attachment";
                    return false;
                }
                compiled.depthAttachment = att;
                compiled.hasDepthAttachment = true;
            } else {
                compiled.colorAttachments.push_back(att);
            }
        }

        const std::string stencil = stencilAttachmentError(passDecl, compiled);
        if (!stencil.empty()) {
            outError = stencil;
            return false;
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Main Compile Entry Point
// ═══════════════════════════════════════════════════════════════

FrameGraphCompiler::CompileResult FrameGraphCompiler::compile(
    VulkanDevice& device,
    const FrameGraphBuilder& builder,
    VkExtent2D referenceExtent,
    uint32_t /*swapchainImageCount*/,
    const ImportedImageMap& importedImages,
    uint32_t maxFramesInFlight,
    const std::unordered_map<std::string, std::shared_ptr<VulkanPipeline>>& manualPipelines)
{
    ensureLogger();

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

    // With async compute, images passes on both queues use are shared by both.
    std::vector<bool> sharedWithCompute;
    if (device.hasAsyncCompute()) {
        sharedWithCompute = sharedAcrossQueues(passes, resources, builder.getDescriptorSetLayouts());
    }

    // Stage 1: Create physical resources. Their shapes (mips and layers) feed
    // everything below, so they exist before the graph is scheduled.
    if (!createPhysicalResources(device, resources, passes, result.physicalResources,
                                 referenceExtent, importedImages, sharedWithCompute, result.errorMessage)) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }
    m_logger->log(LogLevel::Info, "Physical resources created: %zu", result.physicalResources.size());

    // The scheduler's view of each resource, from what was actually created.
    std::vector<ScheduledResource> scheduled(resources.size());
    for (uint32_t i = 0; i < resources.size(); ++i) {
        if (const auto* img = std::get_if<PhysicalImage>(&result.physicalResources[i])) {
            scheduled[i].isImage = true;
            scheduled[i].shape = img->shape;
            scheduled[i].aspect = img->aspect;
            // As describeResources has it. Without this a persistent image's
            // first barrier each frame came from UNDEFINED, which lets the
            // driver drop what initializePersistentImages and the last frame
            // left in it.
            scheduled[i].persistent = !resources[i].imported && resources[i].imageDesc.persistent;
            auto importIt = importedImages.find(i);
            scheduled[i].presentFallback = resources[i].imported && importIt != importedImages.end() &&
                                           importIt->second.views.size() > 1;
        }
    }

    // Stage 2: Mip and layer ranges must fit their images
    if (!validateSubresources(passes, resources, scheduled, builder.getDescriptorSetLayouts(),
                              result.errorMessage)) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }

    // The buffers each pass's descriptors and declared accesses touch, for
    // the indirect passes' order (stage 3) and the buffer barriers (5.1).
    const auto bufferAccesses = describeBufferAccesses(passes, resources, builder.getDescriptorSetLayouts(),
                                                       reflectShaderBufferUse(passes, [&](const std::string& name) {
                                                           return manualPipelines.count(name) > 0;
                                                       }));

    // Stage 3: Order passes by the subresources they read and write, and
    // indirect passes after the writers of their arguments
    auto dependencies = passDependencies(passes, scheduled);
    addIndirectArgumentDependencies(passes, bufferAccesses, dependencies);
    if (!sortPasses(passes, dependencies, result.executionOrder, result.errorMessage)) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }

    // Stage 3.1: Dead pass culling
    {
        const auto live = livePasses(passes, resources, dependencies);
        std::vector<bool> isLive(passes.size(), false);
        for (uint32_t pi : live) isLive[pi] = true;
        const size_t beforeCull = result.executionOrder.size();
        std::erase_if(result.executionOrder, [&](uint32_t pi) { return !isLive[pi]; });
        m_logger->log(LogLevel::Info, "Pass order: %zu passes, %zu after culling",
                      beforeCull, result.executionOrder.size());
    }

    // Stage 3.2: Transient images, created now that their lifetimes are
    // known, in memory they may share
    if (!createTransientImages(device, resources, passes, builder.getDescriptorSetLayouts(), result.executionOrder,
                               result.physicalResources, scheduled, result.errorMessage)) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }

    // Stage 3.5: Create samplers from JSON declarations
    const auto& samplerDescs = builder.getSamplers();
    if (!samplerDescs.empty()) {
        createSamplers(device, samplerDescs, result.samplers);
        m_logger->log(LogLevel::Info, "Samplers created: %zu", result.samplers.size());
    }

    // Stage 4: Initialize compiled passes
    result.compiledPasses.resize(passes.size());
    for (uint32_t i = 0; i < passes.size(); ++i) {
        auto& compiled = result.compiledPasses[i];
        compiled.declIndex = i;
        compiled.executionKind = executionKindOf(passes[i].execution.type);
        for (const auto& pc : passes[i].pushConstants) {
            compiled.pushConstantStages.push_back(JsonUtils::stringsToShaderStages(pc.stages));
        }
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

        // The occlusion image is a read-back R32_SFLOAT image.
        if (!passDecl.execution.occlusion.empty()) {
            const auto& resources = builder.getResourceDeclarations();
            const auto it = std::find_if(resources.begin(), resources.end(), [&](const ResourceDeclaration& r) {
                return r.name == passDecl.execution.occlusion;
            });
            if (it == resources.end() || it->kind != ResourceKind::Image ||
                it->imageDesc.format != VK_FORMAT_R32_SFLOAT || !it->readbackPolicy.enabled) {
                result.errorMessage = "Pass '" + passDecl.name + "': \"occlusion\" names '" +
                                      passDecl.execution.occlusion +
                                      "', which is not an R32_SFLOAT image resource with a readback";
                m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
                return result;
            }
        }
    }

    // Each pass's queue; with async compute, passes on the two queues wait
    // for each other where they must, and are grouped into batches.
    std::vector<QueueType> queues;
    if (device.hasAsyncCompute()) {
        for (const auto& pass : passes) queues.push_back(pass.queueType);
    }
    std::vector<uint32_t> imageWaits, bufferWaits;

    // Stage 5: Barriers, per mip and layer
    {
        auto plan = planBarriers(passes, result.executionOrder, scheduled, queues);
        imageWaits = std::move(plan.crossQueueWait);
        for (uint32_t pi = 0; pi < passes.size(); ++pi) {
            result.compiledPasses[pi].preBarriers  = std::move(plan.preBarriers[pi]);
            result.compiledPasses[pi].postBarriers = std::move(plan.postBarriers[pi]);
        }
        for (uint32_t i = 0; i < resources.size(); ++i) {
            if (auto* img = std::get_if<PhysicalImage>(&result.physicalResources[i])) {
                img->finalLayouts = std::move(plan.finalLayouts[i]);
                img->currentLayout = img->finalLayouts.empty()
                                   ? VK_IMAGE_LAYOUT_UNDEFINED : img->finalLayouts.front();
            }
        }
        for (const auto& warning : plan.warnings) {
            m_logger->log(LogLevel::Warning, "%s", warning.c_str());
        }
        for (uint32_t pi : result.executionOrder) {
            result.compiledPasses[pi].queueType = passes[pi].queueType;
        }
    }
    // Stage 5.1: Buffer barriers, from the buffers each pass's descriptors
    // and declared accesses touch
    {
        const auto memoryBarriers = planBufferBarriers(result.executionOrder, bufferAccesses, queues, &bufferWaits);
        for (uint32_t pi = 0; pi < passes.size(); ++pi) {
            result.compiledPasses[pi].memoryBarrier = memoryBarriers[pi];
        }
    }
    // Stage 5.2: Queue batches, each pass waiting for the later of what its
    // images and its buffers need
    if (queues.empty()) {
        result.queueBatches = {QueueBatch{QueueType::Graphics, result.executionOrder, -1}};
    } else {
        std::vector<uint32_t> position(passes.size(), kNoPass);
        for (uint32_t pos = 0; pos < result.executionOrder.size(); ++pos) position[result.executionOrder[pos]] = pos;
        std::vector<uint32_t> waits(passes.size(), kNoPass);
        for (uint32_t pi = 0; pi < passes.size(); ++pi) {
            for (const auto* list : {&imageWaits, &bufferWaits}) {
                const uint32_t w = pi < list->size() ? (*list)[pi] : kNoPass;
                if (w != kNoPass && (waits[pi] == kNoPass || position[w] > position[waits[pi]])) waits[pi] = w;
            }
        }
        result.queueBatches = planQueueBatches(result.executionOrder, queues, waits);
        size_t compute = 0, withWait = 0;
        for (const auto& batch : result.queueBatches) {
            compute += batch.queue == QueueType::Compute;
            withWait += batch.wait >= 0;
        }
        m_logger->log(LogLevel::Info, "Queue batches: %zu (%zu compute), %zu waiting on the other queue",
                      result.queueBatches.size(), compute, withWait);
        for (size_t b = 0; b < result.queueBatches.size(); ++b) {
            const auto& batch = result.queueBatches[b];
            std::string names;
            for (uint32_t pi : batch.passes) names += (names.empty() ? "" : ", ") + passes[pi].name;
            m_logger->log(LogLevel::Info, "  batch %zu (%s, waits for %d): %s", b,
                          batch.queue == QueueType::Compute ? "compute" : "graphics", batch.wait, names.c_str());
        }
    }

    size_t imageBarrierCount = 0, memoryBarrierCount = 0;
    for (uint32_t pi : result.executionOrder) {
        const auto& compiled = result.compiledPasses[pi];
        imageBarrierCount += compiled.preBarriers.size() + compiled.postBarriers.size();
        memoryBarrierCount += compiled.memoryBarrier.empty() ? 0 : 1;
    }
    m_logger->log(LogLevel::Info, "Barriers planned: %zu image, %zu buffer memory",
                  imageBarrierCount, memoryBarrierCount);

    // Stage 6: Attachments and render areas
    const auto attachmentOps = planAttachmentOps(passes, resources, builder.getDescriptorSetLayouts(),
                                                 result.executionOrder, scheduled);
    if (!createAttachments(result.compiledPasses, result.executionOrder, passes,
                           result.physicalResources, resources, attachmentOps, result.errorMessage)) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }
    {
        size_t discardedLoads = 0, discardedStores = 0;
        for (uint32_t pi : result.executionOrder) {
            const auto& compiled = result.compiledPasses[pi];
            auto count = [&](const CompiledAttachment& att) {
                discardedLoads  += att.loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                discardedStores += att.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE;
            };
            for (const auto& att : compiled.colorAttachments) count(att);
            if (compiled.hasDepthAttachment) count(compiled.depthAttachment);
        }
        m_logger->log(LogLevel::Info, "Attachments: %zu load(s) and %zu store(s) discarded",
                      discardedLoads, discardedStores);
    }

    // Stage 6.5: Regions of copy and blit passes
    for (uint32_t pi : result.executionOrder) {
        auto& compiled = result.compiledPasses[pi];
        if (compiled.executionKind != ExecutionKind::Copy && compiled.executionKind != ExecutionKind::Blit) continue;
        if (!compileTransferRegions(passes[pi], result.physicalResources, resources, compiled,
                                    result.errorMessage)) {
            m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
            return result;
        }
    }

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
    if (!createPipelines(device, result.compiledPasses, result.executionOrder,
                         passes, result.physicalResources, manualPipelines,
                         builder.getVertexFormatRegistry(), result.errorMessage)) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }
    m_logger->log(LogLevel::Info, "Pipelines created");

    // Stage 11: Compile buffer layouts (declarative dot-path)
    const auto& bufferLayoutDescs = builder.getBufferLayouts();
    if (!bufferLayoutDescs.empty()) {
        compileBufferLayouts(bufferLayoutDescs, result.bufferLayouts);
        m_logger->log(LogLevel::Info, "Buffer layouts compiled: %zu layouts", result.bufferLayouts.size());

        // pass.* values change from one pass to the next. Push constants are
        // written while each pass records; buffers are filled once per frame
        // and would only ever see one of them.
        for (const auto& [name, layout] : result.bufferLayouts) {
            if (layout.hasPassSources && !layout.isPushConstant()) {
                result.errorMessage = "Buffer layout '" + name + "' uses a pass.* source; those change "
                                      "from pass to pass, so only push_constant layouts may use them";
                m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
                return result;
            }
        }
    }

    // A material buffer ("source": "material") is a uniform buffer layout
    // filled from the material alone: sets are reused for equal materials.
    for (const auto& setLayout : builder.getDescriptorSetLayouts()) {
        for (const auto& binding : setLayout.bindings) {
            if (binding.source != "material") continue;
            const auto layoutIt = result.bufferLayouts.find(binding.name);
            const auto descIt = std::find_if(bufferLayoutDescs.begin(), bufferLayoutDescs.end(),
                                             [&binding](const auto& desc) { return desc.name == binding.name; });
            std::string problem;
            if (layoutIt == result.bufferLayouts.end() || descIt == bufferLayoutDescs.end()) {
                problem = "names no buffer layout";
            } else if (layoutIt->second.usage != BufferUsageType::UniformBuffer) {
                problem = "names a buffer layout that is not a uniform_buffer";
            } else {
                for (const auto& field : descIt->fields) {
                    if (!field.source.empty() && !field.source.starts_with("entity.material.") &&
                        !field.source.starts_with("const.")) {
                        problem = "has field '" + field.name + "' from '" + field.source +
                                  "'; a material buffer reads only entity.material.* and const.*";
                        break;
                    }
                }
            }
            if (!problem.empty()) {
                result.errorMessage = "Material buffer '" + binding.name + "' of descriptor set layout '" +
                                      setLayout.name + "' " + problem;
                m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
                return result;
            }
        }
    }

    // Stage 11.1: Scene-filled buffers hold what the scene gives them, and
    // indirect passes' argument buffers their arguments
    result.errorMessage = sceneBuffersError(bufferLayoutDescs, result.bufferLayouts);
    if (result.errorMessage.empty()) {
        result.errorMessage = indirectArgumentsError(passes, bufferLayoutDescs, result.bufferLayouts);
    }
    if (!result.errorMessage.empty()) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
    }

    // Stage 12: Check each shader's declared interface against the JSON
    if (!validateShaderInterfaces(builder, result.bufferLayouts, result.errorMessage)) {
        return result;
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
        // A layout with an acceleration structure cannot exist without ray
        // queries, nor one with the scene's textures without bindless
        // textures; only passes left out for lacking them ("requires") use one.
        const auto has = [&](auto&& match) { return std::any_of(desc.bindings.begin(), desc.bindings.end(), match); };
        if (!device.hasRayQuery() && has([](const auto& b) { return b.type == "acceleration_structure"; })) {
            m_logger->log(LogLevel::Info, "  Skipped descriptor set layout '%s': it needs ray queries",
                          desc.name.c_str());
            continue;
        }
        if (!device.hasBindlessTextures() && has([](const auto& b) { return b.source == "sceneTextures"; })) {
            m_logger->log(LogLevel::Info, "  Skipped descriptor set layout '%s': it needs bindless textures",
                          desc.name.c_str());
            continue;
        }

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
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                // Written per frame by RenderGraph with the scene's structure.
                case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
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
                    if (binding.source == "sceneTextures") {
                        // Slots past the scene's textures stay unwritten. The
                        // count leaves room for the stage's other samplers.
                        const auto& limits = device.getLimits();
                        const uint32_t room = std::min({limits.maxPerStageDescriptorSamplers,
                                                        limits.maxPerStageDescriptorSampledImages,
                                                        limits.maxDescriptorSetSamplers,
                                                        limits.maxDescriptorSetSampledImages});
                        const uint32_t fits = room > 256 ? room - 256 : 1;
                        if (img.count > fits) {
                            m_logger->log(LogLevel::Warning, "  Scene textures '%s': %u slots, the device has room "
                                          "for %u", bindingName.c_str(), img.count, fits);
                            img.count = fits;
                        }
                        img.partiallyBound = true;
                    }
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

bool FrameGraphCompiler::createPipelines(
    VulkanDevice& device,
    std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    const std::vector<PhysicalResource>& /*physResources*/,
    const std::unordered_map<std::string, std::shared_ptr<VulkanPipeline>>& manualOverrides,
    const VertexFormatRegistry& vertexFormats,
    std::string& outError)
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
                    outError = "Pass '" + passDecl.name + "': no compute pipeline: " + e.what();
                    return false;
                }
            }
            continue;  // Skip graphics pipeline creation for compute passes
        }

        // ── Ray tracing pass: its pipeline and shader binding table ──
        if (passDecl.type == PassType::RayTracing) {
            const auto& pd = passDecl.pipelineDesc;
            RayTracingShaders shaders;
            shaders.rayGen = pd.rayGenShader;
            shaders.miss = pd.missShaders;
            for (const auto& g : pd.hitGroups) shaders.hitGroups.push_back({g.closestHit, g.anyHit, g.intersection});
            shaders.maxRecursionDepth = pd.maxRecursionDepth;
            std::vector<VkPushConstantRange> pushRanges;
            for (const auto& pc : passDecl.pushConstants) {
                pushRanges.push_back({JsonUtils::stringsToShaderStages(pc.stages), pc.offset, pc.size});
            }
            try {
                compiled.rayTracingPipeline = std::make_shared<VulkanRayTracingPipeline>(
                    device, shaders, compiled.descriptorSetLayouts, pushRanges);
                compiled.pipelineLayout = compiled.rayTracingPipeline->getLayout();
                m_logger->log(LogLevel::Info, "  Created ray tracing pipeline for pass '%s'", passDecl.name.c_str());
            } catch (const std::exception& e) {
                outError = "Pass '" + passDecl.name + "': no ray tracing pipeline: " + e.what();
                return false;
            }
            continue;
        }

        // Skip non-graphics passes
        if (passDecl.type != PassType::Graphics) continue;

        // A graphics pass with no attachments has nothing to render into.
        if (!compiled.rendersAttachments()) continue;

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
        if (pd.vertexShader.empty() && pd.meshShader.empty()) continue;

        // Build pipeline from PipelineDesc
        PipelineStateBuilder builder;

        // Shaders. Without a fragment shader the pipeline is depth only.
        if (!pd.meshShader.empty()) {
            builder.withMeshShaders(pd.taskShader, pd.meshShader, pd.fragmentShader);
        } else {
            builder.withShaders(pd.vertexShader, pd.fragmentShader);
        }
        if (!pd.tessControlShader.empty()) {
            builder.withShader(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, pd.tessControlShader);
            builder.withShader(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, pd.tessEvaluationShader);
            builder.withTessellation(pd.patchControlPoints);
        }
        if (!pd.geometryShader.empty()) builder.withGeometryShader(pd.geometryShader);

        // Vertex input — check declarative registry first, then fall back to hardcoded
        if (pd.vertexInput != "none" && pd.meshShader.empty()) {
            VkVertexInputBindingDescription vtxBinding{};
            std::vector<VkVertexInputBindingDescription> vtxBindings;
            std::vector<VkVertexInputAttributeDescription> vtxAttributes;

            if (vertexFormats.hasFormat(pd.vertexInput) &&
                vertexFormats.getVertexInputDescriptions(pd.vertexInput, vtxBinding, vtxAttributes)) {
                // Use declarative vertex format from JSON
                vtxBindings.push_back(vtxBinding);
            } else if (pd.vertexInput == "default") {
                // Backward compatibility: use hardcoded Vertex struct
                vtxBindings.push_back(Vertex::getBindingDescription());
                const auto attributes = Vertex::getAttributeDescriptions();
                vtxAttributes.assign(attributes.begin(), attributes.end());
            }
            // else: unknown format name — no vertex input (will likely cause validation error)

            if (!vtxBindings.empty()) {
                // Instanced entity passes: each instance's model and previous
                // model matrix at binding 1 (InstanceRow), locations 8 to 15.
                if (passDecl.execution.instancing) {
                    vtxBindings.push_back({kInstanceBinding, sizeof(InstanceRow), VK_VERTEX_INPUT_RATE_INSTANCE});
                    for (uint32_t column = 0; column < 8; ++column) {
                        vtxAttributes.push_back({kInstanceFirstLocation + column, kInstanceBinding,
                                                 VK_FORMAT_R32G32B32A32_SFLOAT, column * 16u});
                    }
                }
                builder.withCustomVertexInput(vtxBindings, vtxAttributes);
                compiled.vertexStride = vtxBindings[0].stride;
            }

            // A declared format may name the first attributes of a mesh's
            // vertex: the stride is bound per draw.
            if (compiled.vertexStride != 0) builder.withDynamicState(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE);
        }
        // "none" = no vertex input (fullscreen triangle), don't call withVertexType

        // Topology
        builder.withTopology(JsonUtils::stringToTopology(pd.topology));

        // Input assembly and rasterization
        builder.withPrimitiveRestart(pd.primitiveRestart);
        builder.withCulling(JsonUtils::stringToCullMode(pd.cullMode),
                            pd.frontFace == "clockwise" ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        if (pd.cullMode == "material") {
            builder.withDynamicState(VK_DYNAMIC_STATE_CULL_MODE);
            builder.withDynamicState(VK_DYNAMIC_STATE_FRONT_FACE);
            compiled.materialCulling = true;
        }
        builder.withLineWidth(pd.lineWidth);
        if (pd.wireframe) builder.withWireframe();
        const VkPhysicalDeviceFeatures& features = device.getEnabledFeatures();
        if (pd.depthBias) {
            float biasClamp = pd.depthBiasClamp;
            if (biasClamp != 0.0f && !features.depthBiasClamp) {
                m_logger->log(LogLevel::Warning,
                    "Pass '%s': depthBias clamp ignored, the device lacks the depthBiasClamp feature",
                    passDecl.name.c_str());
                biasClamp = 0.0f;
            }
            builder.withDepthBias(pd.depthBiasConstant, pd.depthBiasSlope, biasClamp);
        }
        if (pd.depthClamp) {
            if (features.depthClamp) {
                builder.withDepthClamp(true);
            } else {
                m_logger->log(LogLevel::Warning,
                    "Pass '%s': depthClamp ignored, the device lacks the depthClamp feature",
                    passDecl.name.c_str());
            }
        }

        // Depth
        builder.withDepthTest(pd.depthTest, JsonUtils::stringToCompareOp(pd.depthCompareOp));
        builder.withDepthWrite(pd.depthWrite);
        if (pd.stencilTest) builder.withStencil(pd.stencilFront, pd.stencilBack);
        if (pd.depthBoundsTest) {
            if (features.depthBounds) {
                builder.withDepthBounds(pd.minDepthBounds, pd.maxDepthBounds);
            } else {
                m_logger->log(LogLevel::Warning,
                    "Pass '%s': depthBounds ignored, the device lacks the depthBounds feature",
                    passDecl.name.c_str());
            }
        }

        // Blending, one state per colour attachment in output order: an
        // output's own blending and write mask, or the pipeline's
        const auto blends = colorBlendStates(passDecl);
        for (size_t k = 1; k < blends.size(); ++k) {
            if (std::memcmp(&blends[k], &blends[0], sizeof(VkPipelineColorBlendAttachmentState)) != 0 &&
                !features.independentBlend) {
                outError = "Pass '" + passDecl.name + "' blends its colour attachments differently, and this "
                           "device lacks the independentBlend feature";
                return false;
            }
        }
        builder.withAttachmentBlends(blends);
        builder.withBlendConstants(pd.blendConstants);
        if (!pd.blendConstantsPath.empty()) builder.withDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS);

        // Attachment formats, which dynamic rendering takes in place of a
        // render pass
        RenderingFormats formats;
        for (const auto& att : compiled.colorAttachments) {
            formats.color.push_back(att.format);
        }
        if (compiled.hasDepthAttachment) {
            formats.depth = compiled.depthAttachment.format;
        }
        formats.samples = compiled.samples;

        // Always use dynamic viewport/scissor
        builder.withDynamicViewport();
        builder.withDynamicScissor();

        // Descriptor set layouts from auto-created sets (Feature 1)
        for (auto layout : compiled.descriptorSetLayouts) {
            builder.withDescriptorSetLayout(layout);
        }

        // Push constant ranges
        uint32_t pushEnd = 0;
        for (const auto& pc : passDecl.pushConstants) {
            VkShaderStageFlags stages = JsonUtils::stringsToShaderStages(pc.stages);
            builder.withPushConstants(stages, pc.size, pc.offset);
            pushEnd = std::max(pushEnd, pc.offset + pc.size);
        }
        if (pushEnd > device.getLimits().maxPushConstantsSize) {
            m_logger->log(LogLevel::Error,
                "  Pass '%s' uses %u bytes of push constants; this device allows %u. The pass draws nothing",
                passDecl.name.c_str(), pushEnd, device.getLimits().maxPushConstantsSize);
            continue;
        }

        // Build the pipeline
        try {
            compiled.pipeline = builder.buildPipeline(device, formats, compiled.extent);
            compiled.pipelineLayout = compiled.pipeline->getLayout();
            m_logger->log(LogLevel::Info, "  Auto-created pipeline for pass '%s'",
                          passDecl.name.c_str());
        } catch (const std::exception& e) {
            outError = "Pass '" + passDecl.name + "': no graphics pipeline: " + e.what();
            return false;
        }
    }
    return true;
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
    VulkanDevice& /*device*/,
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

                // The whole image, or the mips and layers the binding names
                VkImageView view = physImg->view;
                if (!binding.autoBindSubresource.isWholeImage() && physImg->subresourceViews) {
                    const auto range = physImg->shape.resolve(binding.autoBindSubresource);
                    const auto kind = builder.getResourceDeclarations()[resourceHandle.index].imageDesc.viewType;
                    VkImageSubresourceRange vr{};
                    vr.aspectMask     = isDepthFormat(physImg->format) ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT}
                                                                     : physImg->aspect;
                    vr.baseMipLevel   = range.baseMip;
                    vr.levelCount     = range.mipCount;
                    vr.baseArrayLayer = range.baseLayer;
                    vr.layerCount     = range.layerCount;
                    view = physImg->subresourceViews->get(physImg->vkImage, physImg->format,
                                                          sampledViewType(kind, physImg->shape, range), vr);
                }

                // Bind to all frames
                for (uint32_t frame = 0; frame < maxFramesInFlight; ++frame) {
                    ImageResource imgRes{view, sampler, layout};
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


// CompiledBufferLayout is now in Shoonyakasha::FrameGraph namespace
VkShaderStageFlags CompiledBufferLayout::getShaderStages() const {
    return JsonUtils::stringsToShaderStages(binding.stages);
}

namespace {

/// Map a shader-side field type onto what the dot-path resolver can produce.
/// BufferFieldType has 17 members and ResolvedValue's variant has 8 value
/// alternatives, so double, bool, the integer vectors and mat2 have no
/// representation. Those fields still pack correctly and are marked
/// unresolvable, so writeField leaves them zeroed rather than writing a float
/// into, for example, a uvec4 slot.
bool toResolverType(BufferFieldType t, Shoonyakasha::MaterialParam::Type& out) {
    using MT = Shoonyakasha::MaterialParam::Type;
    switch (t) {
        case BufferFieldType::Float: out = MT::Float; return true;
        case BufferFieldType::Int:   out = MT::Int;   return true;
        case BufferFieldType::UInt:  out = MT::UInt;  return true;
        case BufferFieldType::Vec2:  out = MT::Vec2;  return true;
        case BufferFieldType::Vec3:  out = MT::Vec3;  return true;
        case BufferFieldType::Vec4:  out = MT::Vec4;  return true;
        case BufferFieldType::Mat3:  out = MT::Mat3;  return true;
        case BufferFieldType::Mat4:  out = MT::Mat4;  return true;
        default:                     out = MT::Float; return false;
    }
}

/// A field's JSON "default" as the value the resolver writes, or an invalid
/// value when there is none or its component count does not fit the type.
Shoonyakasha::ResolvedValue defaultAsResolvedValue(const BufferFieldDesc& f) {
    return resolvedValueOf(f.type, f.defaultValue);
}

} // namespace

Shoonyakasha::ResolvedValue resolvedValueOf(BufferFieldType type, const std::vector<float>& d) {
    switch (type) {
        case BufferFieldType::Float: if (d.size() == 1) return Shoonyakasha::ResolvedValue(d[0]); break;
        case BufferFieldType::Int:   if (d.size() == 1) return Shoonyakasha::ResolvedValue(static_cast<int32_t>(d[0])); break;
        case BufferFieldType::UInt:  if (d.size() == 1) return Shoonyakasha::ResolvedValue(static_cast<uint32_t>(d[0])); break;
        case BufferFieldType::Vec2:  if (d.size() == 2) return Shoonyakasha::ResolvedValue(glm::vec2(d[0], d[1])); break;
        case BufferFieldType::Vec3:  if (d.size() == 3) return Shoonyakasha::ResolvedValue(glm::vec3(d[0], d[1], d[2])); break;
        case BufferFieldType::Vec4:  if (d.size() == 4) return Shoonyakasha::ResolvedValue(glm::vec4(d[0], d[1], d[2], d[3])); break;
        case BufferFieldType::Mat4:
            if (d.size() == 16) {
                glm::mat4 m;
                for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) m[c][r] = d[static_cast<size_t>(c * 4 + r)];
                return Shoonyakasha::ResolvedValue(m);
            }
            break;
        default: break;
    }
    return Shoonyakasha::ResolvedValue();
}

Shoonyakasha::CompiledBufferLayout CompiledBufferLayout::toResolverLayout() const {
    Shoonyakasha::CompiledBufferLayout out;
    out.name             = name;
    out.totalSize        = totalSize;
    out.hasSceneSources  = hasSceneSources;
    out.hasEntitySources = hasEntitySources;
    out.hasConstSources  = hasConstSources;
    out.hasPassSources   = hasPassSources;
    out.fields.reserve(fields.size());

    for (const auto& f : fields) {
        Shoonyakasha::BufferField rf;
        rf.name         = f.name;
        rf.source       = f.source;
        rf.compiled     = DotPathResolver().compile(f.source);
        rf.offset       = f.offset;
        rf.size         = f.size;
        rf.arrayCount   = f.arrayCount;
        rf.arrayStride  = f.arrayStride;
        rf.columnStride = f.columnStride;
        rf.resolvable   = toResolverType(f.type, rf.type);
        rf.fallback     = defaultAsResolvedValue(f);
        out.fields.push_back(std::move(rf));
    }
    return out;
}

void FrameGraphCompiler::ensureLogger() {
    if (!m_logger) {
        m_logger = new Logger("framegraph_compiler.log");
    }
}

bool FrameGraphCompiler::validateShaderInterfaces(
    const FrameGraphBuilder& builder,
    const std::unordered_map<std::string, CompiledBufferLayout>& layouts,
    std::string& outError)
{
    ensureLogger();
    std::vector<std::string> problems;

    for (const auto& pass : builder.getPassDeclarations()) {
        const auto& pd = pass.pipelineDesc;
        std::vector<const std::string*> shaders;
        pd.forEachShader([&](const std::string& path, VkShaderStageFlagBits) { shaders.push_back(&path); });

        ShaderInterfaceExpectation expected;
        expected.setCount = static_cast<uint32_t>(pass.descriptorSetRefs.size());
        for (uint32_t set = 0; set < expected.setCount; ++set) {
            const auto* setDesc = builder.getDescriptorSetLayout(pass.descriptorSetRefs[set]);
            if (!setDesc) continue;
            for (const auto& binding : setDesc->bindings) {
                ExpectedDescriptor descriptor;
                try {
                    descriptor.type = JsonUtils::stringToDescriptorType(binding.type);
                } catch (const std::exception&) {
                    continue;
                }
                descriptor.setName = setDesc->name;
                descriptor.bindingName = binding.name;
                const std::string& bufferLayout = binding.source == "material" ? binding.name : binding.autoBindBuffer;
                if (!bufferLayout.empty()) {
                    const auto it = layouts.find(bufferLayout);
                    if (it != layouts.end()) descriptor.layout = &it->second;
                }
                expected.descriptors[{set, binding.binding}] = descriptor;
            }
        }
        for (const auto& range : pass.pushConstants) {
            expected.pushConstantSize = std::max(expected.pushConstantSize, range.offset + range.size);
        }
        if (!pass.execution.entityDataBinding.empty()) {
            const auto* config = builder.getEntityDataBinding(pass.execution.entityDataBinding);
            if (config && !config->perDraw.layoutRef.empty()) {
                const auto it = layouts.find(config->perDraw.layoutRef);
                if (it != layouts.end()) expected.pushConstantLayout = &it->second;
            }
        }

        for (const std::string* path : shaders) {
            if (path->empty()) continue;
            std::ifstream file(*path, std::ios::binary | std::ios::ate);
            if (!file) continue;
            std::vector<char> code(static_cast<size_t>(file.tellg()));
            file.seekg(0);
            file.read(code.data(), static_cast<std::streamsize>(code.size()));

            for (const auto& message : validateShaderInterface(code.data(), code.size(), expected)) {
                problems.push_back("pass '" + pass.name + "', " + *path + ": " + message);
            }
        }
    }

    if (problems.empty()) return true;

    outError = "Shader interfaces do not match the pipeline JSON:";
    for (const auto& problem : problems) {
        m_logger->log(LogLevel::Error, "%s", problem.c_str());
        outError += "\n  " + problem;
    }
    return false;
}

void FrameGraphCompiler::compileBufferLayouts(
    const std::vector<BufferLayoutDesc>& layoutDescs,
    std::unordered_map<std::string, CompiledBufferLayout>& outLayouts)
{
    ensureLogger();

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

        // Packing: one shared implementation of the std140 / std430 / scalar
        // rules, in FrameGraph/BufferFieldTypes.h. Previously only Std140 was
        // handled here and Std430, Scalar and PushConstant all fell into a
        // branch that applied no alignment at all.
        uint32_t currentOffset = 0;
        uint32_t maxMemberAlignment = 1;

        for (auto& field : compiled.fields) {
            const uint32_t explicitOffset = field.offset;
            const PackedField packed = packField(
                field.type, desc.packing, field.arrayCount,
                field.hasExplicitOffset ? &explicitOffset : nullptr,
                currentOffset);

            field.offset       = packed.offset;
            field.size         = packed.size;
            field.columnStride = packed.columnStride;
            field.arrayStride  = packed.arrayStride;

            maxMemberAlignment = std::max(maxMemberAlignment,
                                          baseAlignment(field.type, desc.packing));
        }

        compiled.totalSize = blockSize(currentOffset, desc.packing, maxMemberAlignment);

        // Vulkan guarantees 128 bytes of push constants (256 from Vulkan 1.4,
        // and on every desktop GPU). createPipelines checks the device's own
        // limit for each pass.
        if (desc.usage == BufferUsageType::PushConstant && compiled.totalSize > 128) {
            m_logger->log(LogLevel::Info,
                "  Push constant layout '%s' is %u bytes, over the 128 bytes every Vulkan device allows",
                desc.name.c_str(), compiled.totalSize);
        }

        // Classify sources
        compiled.hasSceneSources = false;
        compiled.hasEntitySources = false;
        compiled.hasConstSources = false;
        compiled.hasPassSources = false;

        // Validate every source once, here, at compile time.
        //
        // An unresolvable dot-path produces zeros at runtime with no log line at
        // any severity, no counter and no return value — for an engine whose
        // premise is that the JSON *is* the pipeline, a typo'd `source` was the
        // quietest possible failure. DotPathResolver::validatePath existed for
        // exactly this and had no production caller.
        Shoonyakasha::DotPathResolver validator;

        for (const auto& field : compiled.fields) {
            if (!field.defaultValue.empty() && !defaultAsResolvedValue(field).isValid()) {
                m_logger->log(LogLevel::Warning,
                              "  Buffer layout '%s' field '%s': \"default\" has %zu values, which does not "
                              "fit the field's type; it is ignored.",
                              desc.name.c_str(), field.name.c_str(), field.defaultValue.size());
            }
            if (field.source.empty()) continue;

            if (field.source.starts_with("scene.")) {
                compiled.hasSceneSources = true;
            } else if (field.source.starts_with("entity.")) {
                compiled.hasEntitySources = true;
            } else if (field.source.starts_with("const.")) {
                compiled.hasConstSources = true;
            } else if (field.source.starts_with("pass.")) {
                compiled.hasPassSources = true;
            }

            const std::string problem = validator.validatePath(field.source);
            if (!problem.empty()) {
                m_logger->log(LogLevel::Warning,
                              "  Buffer layout '%s' field '%s': %s. The field will be zeroed every frame.",
                              desc.name.c_str(), field.name.c_str(), problem.c_str());
            }

            Shoonyakasha::MaterialParam::Type unusedType;
            if (!toResolverType(field.type, unusedType)) {
                m_logger->log(LogLevel::Warning,
                              "  Buffer layout '%s' field '%s' has a shader type the dot-path "
                              "resolver cannot produce; it is packed correctly but left zeroed.",
                              desc.name.c_str(), field.name.c_str());
            }
        }

        const char* usageStr = desc.usage == BufferUsageType::PushConstant ? "push_constant" :
                               desc.usage == BufferUsageType::UniformBuffer ? "uniform_buffer" :
                               desc.usage == BufferUsageType::StorageBuffer ? "storage_buffer" : "unknown";
        const char* freqStr = desc.updateFrequency == BufferUpdateFrequency::PerFrame ? ", per_frame" : "";

        // Log before the move: this used to read `compiled` after std::move.
        // Report totalSize rather than the raw cursor, since the two differ once
        // the block is rounded to its final alignment.
        m_logger->log(LogLevel::Info, "  Compiled buffer layout '%s' (%s%s, %u bytes, %zu fields, %s%s)",
                      desc.name.c_str(), usageStr, freqStr,
                      compiled.totalSize, desc.fields.size(),
                      toString(desc.packing),
                      compiled.usesDotPathSources() ? ", dot-path sources" : "");

        outLayouts[desc.name] = std::move(compiled);
    }
}

} // namespace FrameGraph
} // namespace Shoonyakasha
