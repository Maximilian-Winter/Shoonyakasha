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
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanMemoryAllocator.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "FrameGraph/ShaderInterfaceValidator.h"
#include "Core/Logger.h"

#include <fstream>
#include <queue>
#include <unordered_set>
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
// Physical Resource Creation
// ═══════════════════════════════════════════════════════════════

bool FrameGraphCompiler::createPhysicalResources(
    VulkanDevice& device,
    const std::vector<ResourceDeclaration>& declarations,
    const std::vector<PassDeclaration>& passes,
    std::vector<PhysicalResource>& outResources,
    VkExtent2D referenceExtent,
    const ImportedImageMap& importedImages,
    std::string& outError)
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
            const uint32_t maxMips = fullMipChainLength(width, height);
            const uint32_t mipLevels = desc.mipLevels == 0 ? maxMips : desc.mipLevels;
            if (mipLevels > maxMips) {
                outError = "Resource '" + decl.name + "' asks for " + std::to_string(mipLevels) +
                           " mip levels; " + std::to_string(width) + "x" + std::to_string(height) +
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

            auto image = std::make_unique<VulkanImage>(
                device, width, height, format,
                VK_IMAGE_TILING_OPTIMAL, usage, memProps,
                shape.mipLevels, shape.arrayLayers,
                cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0
            );

            // The whole-image view shaders sample. A sampled depth/stencil view
            // may name only one aspect, so depth formats are viewed as depth.
            const VkImageAspectFlags fullAspect = Shoonyakasha::formatToAspectMask(format);
            const VkImageAspectFlags sampledAspect =
                Shoonyakasha::isDepthFormat(format) ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT} : fullAspect;
            SubresourceRange whole;
            const VkImageViewType viewType = sampledViewType(desc.viewType, shape, shape.resolve(whole));
            image->createImageView(sampledAspect, viewType);

            PhysicalImage physImg;
            physImg.vkImage = image->getImage();
            physImg.view = image->getImageView();
            physImg.format = format;
            physImg.extent = {width, height};
            physImg.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            physImg.shape = shape;
            physImg.viewType = viewType;
            physImg.aspect = fullAspect;
            physImg.ownedImage = std::move(image);
            physImg.subresourceViews = std::make_unique<ImageViewSet>(device.getLogicalDevice());

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
    return true;
}

// Queue family index a pass of this queue type runs on
static uint32_t resolveQueueFamily(QueueType type, VulkanDevice& device) {
    switch (type) {
        case QueueType::Compute:
            return device.getComputeQueueFamily();
        case QueueType::Graphics:
        default:
            return device.getGraphicsQueueFamily();
    }
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
    std::string& outError)
{
    for (uint32_t execIdx : executionOrder) {
        auto& compiled = compiledPasses[execIdx];
        const auto& passDecl = passes[compiled.declIndex];

        compiled.colorAttachments.clear();
        compiled.hasDepthAttachment = false;
        compiled.extent = {};
        compiled.layerCount = 1;
        bool haveExtent = false;

        // Compute passes render nothing; their extent, which compute_image
        // dispatches over, is that of the first image they write at the mip
        // level they write, or failing that the first image they read.
        if (passDecl.type != PassType::Graphics) {
            for (const auto* list : {&passDecl.outputs, &passDecl.inputs}) {
                for (const auto& access : *list) {
                    if (haveExtent || !access.handle.valid()) continue;
                    const auto* physImg = std::get_if<PhysicalImage>(&physResources[access.handle.index]);
                    if (!physImg) continue;
                    compiled.extent = physImg->mipExtent(physImg->shape.resolve(access.subresource).baseMip);
                    haveExtent = true;
                }
            }
            continue;
        }

        for (const auto& output : passDecl.outputs) {
            if (!output.handle.valid() || !isAttachmentUsage(output.usage)) continue;
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
            att.loadOp   = output.hasClearValue ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            // A read-only depth attachment must keep its contents for later
            // readers; STORE_OP_NONE leaves them untouched without writing.
            att.storeOp  = output.usage == ResourceUsage::DepthStencilReadOnly
                         ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
            if (output.hasClearValue) {
                att.clearValue = output.clearValue;
            } else if (isDepth) {
                att.clearValue.depthStencil = {1.0f, 0};
            } else {
                att.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            }

            if (!declarations[ri].imported) {
                const bool wholeSingle = physImg->shape.mipLevels == 1 && physImg->shape.arrayLayers == 1 &&
                                         physImg->viewType == VK_IMAGE_VIEW_TYPE_2D;
                if (wholeSingle) {
                    att.view = physImg->view;
                } else {
                    VkImageSubresourceRange vr{};
                    vr.aspectMask     = isDepth ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT} : physImg->aspect;
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

    // Stage 1: Create physical resources. Their shapes (mips and layers) feed
    // everything below, so they exist before the graph is scheduled.
    if (!createPhysicalResources(device, resources, passes, result.physicalResources,
                                 referenceExtent, importedImages, result.errorMessage)) {
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

    // Stage 3: Order passes by the subresources they read and write
    const auto dependencies = passDependencies(passes, scheduled);
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

    // Stage 5: Barriers, per mip and layer
    {
        auto plan = planBarriers(passes, result.executionOrder, scheduled);
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
    m_logger->log(LogLevel::Info, "Barriers planned");

    // Stage 6: Attachments and render areas
    if (!createAttachments(result.compiledPasses, result.executionOrder, passes,
                           result.physicalResources, resources, result.errorMessage)) {
        m_logger->log(LogLevel::Error, "%s", result.errorMessage.c_str());
        return result;
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

VkBlendFactor FrameGraphCompiler::stringToBlendFactor(const std::string& str) {
    if (str == "zero")                       return VK_BLEND_FACTOR_ZERO;
    if (str == "one")                        return VK_BLEND_FACTOR_ONE;
    if (str == "src_color")                  return VK_BLEND_FACTOR_SRC_COLOR;
    if (str == "one_minus_src_color")        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    if (str == "dst_color")                  return VK_BLEND_FACTOR_DST_COLOR;
    if (str == "one_minus_dst_color")        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    if (str == "src_alpha")                  return VK_BLEND_FACTOR_SRC_ALPHA;
    if (str == "one_minus_src_alpha")        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (str == "dst_alpha")                  return VK_BLEND_FACTOR_DST_ALPHA;
    if (str == "one_minus_dst_alpha")        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    if (str == "constant_color")             return VK_BLEND_FACTOR_CONSTANT_COLOR;
    if (str == "one_minus_constant_color")   return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    if (str == "constant_alpha")             return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    if (str == "one_minus_constant_alpha")   return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    if (str == "src_alpha_saturate")         return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    return VK_BLEND_FACTOR_ONE;  // default
}

VkBlendOp FrameGraphCompiler::stringToBlendOp(const std::string& str) {
    if (str == "add")              return VK_BLEND_OP_ADD;
    if (str == "subtract")         return VK_BLEND_OP_SUBTRACT;
    if (str == "reverse_subtract") return VK_BLEND_OP_REVERSE_SUBTRACT;
    if (str == "min")              return VK_BLEND_OP_MIN;
    if (str == "max")              return VK_BLEND_OP_MAX;
    return VK_BLEND_OP_ADD;  // default
}

void FrameGraphCompiler::createPipelines(
    VulkanDevice& device,
    std::vector<CompiledPass>& compiledPasses,
    const std::vector<uint32_t>& executionOrder,
    const std::vector<PassDeclaration>& passes,
    const std::vector<PhysicalResource>& /*physResources*/,
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

        // Blending
        if (pd.blending == "alpha") builder.withAlphaBlending();
        else if (pd.blending == "additive") builder.withAdditiveBlending();
        else if (pd.blending == "custom") {
            builder.withCustomBlending(
                stringToBlendFactor(pd.srcColorBlendFactor),
                stringToBlendFactor(pd.dstColorBlendFactor),
                stringToBlendOp(pd.colorBlendOp),
                stringToBlendFactor(pd.srcAlphaBlendFactor),
                stringToBlendFactor(pd.dstAlphaBlendFactor),
                stringToBlendOp(pd.alphaBlendOp));
        }

        // Attachment formats, which dynamic rendering takes in place of a
        // render pass
        RenderingFormats formats;
        for (const auto& att : compiled.colorAttachments) {
            formats.color.push_back(att.format);
        }
        if (compiled.hasDepthAttachment) {
            formats.depth = compiled.depthAttachment.format;
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
            compiled.pipeline = builder.buildPipeline(device, formats, compiled.extent);
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
    const std::vector<PassDeclaration>& /*passes*/,
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

} // namespace

Shoonyakasha::CompiledBufferLayout CompiledBufferLayout::toResolverLayout() const {
    Shoonyakasha::CompiledBufferLayout out;
    out.name             = name;
    out.totalSize        = totalSize;
    out.hasSceneSources  = hasSceneSources;
    out.hasEntitySources = hasEntitySources;
    out.hasConstSources  = hasConstSources;
    out.fields.reserve(fields.size());

    for (const auto& f : fields) {
        Shoonyakasha::BufferField rf;
        rf.name         = f.name;
        rf.source       = f.source;
        rf.offset       = f.offset;
        rf.size         = f.size;
        rf.arrayCount   = f.arrayCount;
        rf.arrayStride  = f.arrayStride;
        rf.columnStride = f.columnStride;
        rf.resolvable   = toResolverType(f.type, rf.type);
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
        const std::string* shaders[] = {&pd.vertexShader, &pd.fragmentShader, &pd.computeShader};

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
                if (!binding.autoBindBuffer.empty()) {
                    const auto it = layouts.find(binding.autoBindBuffer);
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

        // Validate every source once, here, at compile time.
        //
        // An unresolvable dot-path produces zeros at runtime with no log line at
        // any severity, no counter and no return value — for an engine whose
        // premise is that the JSON *is* the pipeline, a typo'd `source` was the
        // quietest possible failure. DotPathResolver::validatePath existed for
        // exactly this and had no production caller.
        Shoonyakasha::DotPathResolver validator;

        for (const auto& field : compiled.fields) {
            if (field.source.empty()) continue;

            if (field.source.starts_with("scene.")) {
                compiled.hasSceneSources = true;
            } else if (field.source.starts_with("entity.")) {
                compiled.hasEntitySources = true;
            } else if (field.source.starts_with("const.")) {
                compiled.hasConstSources = true;
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
