//
// Shoonyakasha Engine - Frame Graph Executor
//
// 朱雀司變  熾熱而速達
// The Vermilion Bird governs transformation — blazing heat, swift arrival
//

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "GPU/VkFormatUtils.h"
#include "Vulkan/FrameGraph/FrameGraphDebugger.h"
#include "Vulkan/FrameGraph/RenderStats.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanDevice.h"
#include "Core/Logger.h"

#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <optional>
#include <array>
#include <cassert>

#include "Vulkan/FrameGraph/FrameGraphJson.h"

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// PassExecuteContext — resource accessor implementations
// ═══════════════════════════════════════════════════════════════

// Resource accessor implementations — cast from opaque pointer to typed vector
static const std::vector<PhysicalResource>& getPhysResources(const PassExecuteContext& ctx) {
    return *static_cast<const std::vector<PhysicalResource>*>(ctx.physicalResourcesPtr);
}

VkImageView PassExecuteContext::getImageView(ResourceHandle h) const {
    if (!h.valid() || !physicalResourcesPtr) return VK_NULL_HANDLE;
    const auto& resources = getPhysResources(*this);
    if (h.index >= resources.size()) return VK_NULL_HANDLE;

    if (const auto* img = std::get_if<PhysicalImage>(&resources[h.index])) {
        return img->view;
    }
    return VK_NULL_HANDLE;
}

VkImage PassExecuteContext::getImage(ResourceHandle h) const {
    if (!h.valid() || !physicalResourcesPtr) return VK_NULL_HANDLE;
    const auto& resources = getPhysResources(*this);
    if (h.index >= resources.size()) return VK_NULL_HANDLE;

    if (const auto* img = std::get_if<PhysicalImage>(&resources[h.index])) {
        return img->vkImage;
    }
    return VK_NULL_HANDLE;
}

VkBuffer PassExecuteContext::getBuffer(ResourceHandle h) const {
    if (!h.valid() || !physicalResourcesPtr) return VK_NULL_HANDLE;
    const auto& resources = getPhysResources(*this);
    if (h.index >= resources.size()) return VK_NULL_HANDLE;

    if (const auto* buf = std::get_if<PhysicalBuffer>(&resources[h.index])) {
        return buf->vkBuffer;
    }
    return VK_NULL_HANDLE;
}

std::shared_ptr<VulkanDescriptorSet> PassExecuteContext::getDescriptorSet(uint32_t setIndex) const {
    if (!descriptorSets || setIndex >= descriptorSets->size()) return nullptr;
    return (*descriptorSets)[setIndex];
}

// ═══════════════════════════════════════════════════════════════
// Frame Graph Executor
// ═══════════════════════════════════════════════════════════════

FrameGraphExecutor::FrameGraphExecutor(VulkanDevice& device, VulkanCommandManager& cmdManager)
    : m_device(device)
    , m_cmdManager(cmdManager)
{
    m_logger = new Logger("framegraph_executor.log");
}

FrameGraphExecutor::~FrameGraphExecutor() {
    delete m_logger;
}

namespace {

/// A synchronization2 stage mask for a planned one. The legacy bits keep
/// their values; TOP_OF_PIPE as a source and BOTTOM_OF_PIPE as a destination
/// mean "nothing", which synchronization2 spells NONE.
VkPipelineStageFlags2 srcStage2(VkPipelineStageFlags stages) {
    return stages == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT ? VK_PIPELINE_STAGE_2_NONE
                                                       : static_cast<VkPipelineStageFlags2>(stages);
}
VkPipelineStageFlags2 dstStage2(VkPipelineStageFlags stages) {
    return stages == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT ? VK_PIPELINE_STAGE_2_NONE
                                                          : static_cast<VkPipelineStageFlags2>(stages);
}

/// Begin dynamic rendering into the pass's attachments. Returns false, having
/// recorded nothing, if the pass has no attachments or one has no view yet
/// (an imported image that was never provided).
bool beginRendering(VkCommandBuffer commandBuffer,
                    const CompiledPass& pass,
                    const FrameGraphCompiler::CompileResult& compiled,
                    const VkRect2D& area,
                    std::vector<VkRenderingAttachmentInfo>& colors)
{
    if (!pass.rendersAttachments() || area.extent.width == 0 || area.extent.height == 0) {
        return false;
    }

    auto toInfo = [&](const CompiledAttachment& att, VkRenderingAttachmentInfo& info) {
        VkImageView view = att.view;
        if (view == VK_NULL_HANDLE) {
            // Imported: the view for this frame's swapchain image.
            const auto* physImg = std::get_if<PhysicalImage>(
                &compiled.physicalResources[att.resource.index]);
            view = physImg ? physImg->view : VK_NULL_HANDLE;
        }
        info = VkRenderingAttachmentInfo{};
        info.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageView   = view;
        info.imageLayout = att.layout;
        info.loadOp      = att.loadOp;
        info.storeOp     = att.storeOp;
        info.clearValue  = att.clearValue;
        return view != VK_NULL_HANDLE;
    };

    colors.resize(pass.colorAttachments.size());
    for (size_t i = 0; i < colors.size(); ++i) {
        if (!toInfo(pass.colorAttachments[i], colors[i])) return false;
    }
    VkRenderingAttachmentInfo depth{};
    if (pass.hasDepthAttachment && !toInfo(pass.depthAttachment, depth)) return false;

    VkRenderingInfo rendering{};
    rendering.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea           = area;
    rendering.layerCount           = pass.layerCount;
    rendering.colorAttachmentCount = static_cast<uint32_t>(colors.size());
    rendering.pColorAttachments    = colors.data();
    rendering.pDepthAttachment     = pass.hasDepthAttachment ? &depth : nullptr;

    vkCmdBeginRendering(commandBuffer, &rendering);
    return true;
}

} // namespace

void FrameGraphExecutor::recordBarriers(VkCommandBuffer commandBuffer,
                                        const std::vector<BarrierInfo>& barriers,
                                        const MemoryBarrierInfo* memory,
                                        const FrameGraphCompiler::CompileResult& compiled,
                                        const std::vector<ResourceDeclaration>& resources,
                                        bool notifyDebugger)
{
    m_imageBarriers.clear();
    for (const auto& barrier : barriers) {
        if (!barrier.resource.valid()) continue;
        const auto* physImg = std::get_if<PhysicalImage>(
            &compiled.physicalResources[barrier.resource.index]);
        if (!physImg || physImg->vkImage == VK_NULL_HANDLE) continue;

        if (m_debugger && notifyDebugger) {
            std::string resourceName = (barrier.resource.index < resources.size())
                ? resources[barrier.resource.index].name : "unknown";
            bool isQueueTransfer = barrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED;
            m_debugger->onBarrierInserted(resourceName, barrier.oldLayout,
                                          barrier.newLayout, isQueueTransfer);
        }

        // Outside a queue transfer the family indices hold
        // VK_QUEUE_FAMILY_IGNORED, so one form covers both cases.
        VkImageMemoryBarrier2 b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask        = srcStage2(barrier.srcStage);
        b.srcAccessMask       = barrier.srcAccess;
        b.dstStageMask        = dstStage2(barrier.dstStage);
        b.dstAccessMask       = barrier.dstAccess;
        b.oldLayout           = barrier.oldLayout;
        b.newLayout           = barrier.newLayout;
        b.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
        b.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
        b.image               = physImg->vkImage;
        b.subresourceRange    = barrier.range;
        m_imageBarriers.push_back(b);
    }

    VkMemoryBarrier2 memoryBarrier{};
    const bool hasMemory = memory && !memory->empty();
    if (hasMemory) {
        memoryBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memoryBarrier.srcStageMask  = srcStage2(memory->srcStage);
        memoryBarrier.srcAccessMask = memory->srcAccess;
        memoryBarrier.dstStageMask  = dstStage2(memory->dstStage);
        memoryBarrier.dstAccessMask = memory->dstAccess;
    }
    if (m_imageBarriers.empty() && !hasMemory) return;

    // One call for the whole set. Each barrier keeps its own stages, so the
    // GPU waits exactly as long as it did with a call per barrier, but
    // drivers see one dependency rather than a chain of them.
    VkDependencyInfo dependency{};
    dependency.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount       = hasMemory ? 1u : 0u;
    dependency.pMemoryBarriers          = hasMemory ? &memoryBarrier : nullptr;
    dependency.imageMemoryBarrierCount  = static_cast<uint32_t>(m_imageBarriers.size());
    dependency.pImageMemoryBarriers     = m_imageBarriers.data();
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

VkRect2D FrameGraphExecutor::viewportRect(const glm::vec4& fractions, VkExtent2D extent) {
    const auto edge = [](float f, uint32_t size) {
        const float texels = std::round(std::clamp(f, 0.0f, 1.0f) * static_cast<float>(size));
        return static_cast<int32_t>(texels);
    };
    const int32_t x0 = edge(fractions.x, extent.width);
    const int32_t y0 = edge(fractions.y, extent.height);
    const int32_t x1 = edge(fractions.x + fractions.z, extent.width);
    const int32_t y1 = edge(fractions.y + fractions.w, extent.height);
    VkRect2D rect{};
    rect.offset = {x0, y0};
    rect.extent = {static_cast<uint32_t>(std::max(x1 - x0, 0)), static_cast<uint32_t>(std::max(y1 - y0, 0))};
    return rect;
}

VkRect2D FrameGraphExecutor::renderArea(const PassDeclaration& passDecl, const CompiledPass& compiledPass) const {
    VkRect2D full{{0, 0}, compiledPass.extent};
    if (passDecl.viewport.empty() || !m_viewportResolver) return full;
    glm::vec4 fractions(0.0f);
    if (!m_viewportResolver(passDecl.viewport, fractions)) {
        if (m_logger) {
            m_logger->logEvery(5.0f, LogLevel::Warning, "Pass '%s': viewport '%s' has no vec4 value; "
                               "the pass renders nothing", passDecl.name.c_str(), passDecl.viewport.c_str());
        }
        return VkRect2D{};
    }
    return viewportRect(fractions, compiledPass.extent);
}

void FrameGraphExecutor::execute(
    const FrameGraphCompiler::CompileResult& compiled,
    const FrameGraphBuilder& builder,
    uint32_t frameIndex,
    uint32_t swapchainImageIndex,
    VkCommandBuffer commandBuffer,
    const std::unordered_map<std::string, ParameterValue>* parameters)
{
    if (!compiled.valid) {
        m_logger->log(LogLevel::Error, "Cannot execute invalid frame graph");
        return;
    }

    // Notify debugger of frame begin
    if (m_debugger) {
        m_debugger->onFrameBegin(frameIndex);
    }

    recordPasses(compiled, builder, compiled.executionOrder, frameIndex, swapchainImageIndex,
                 commandBuffer, parameters);

    // Notify debugger of frame end
    if (m_debugger) {
        m_debugger->onFrameEnd(frameIndex);
    }
}

void FrameGraphExecutor::executePasses(
    const FrameGraphCompiler::CompileResult& compiled,
    const FrameGraphBuilder& builder,
    const std::vector<uint32_t>& passIndices,
    uint32_t frameIndex,
    uint32_t swapchainImageIndex,
    VkCommandBuffer commandBuffer,
    const std::unordered_map<std::string, ParameterValue>* parameters)
{
    if (!compiled.valid) {
        m_logger->log(LogLevel::Error, "Cannot execute invalid frame graph");
        return;
    }
    recordPasses(compiled, builder, passIndices, frameIndex, swapchainImageIndex,
                 commandBuffer, parameters);
}

void FrameGraphExecutor::recordPasses(
    const FrameGraphCompiler::CompileResult& compiled,
    const FrameGraphBuilder& builder,
    const std::vector<uint32_t>& passIndices,
    uint32_t frameIndex,
    uint32_t swapchainImageIndex,
    VkCommandBuffer commandBuffer,
    const std::unordered_map<std::string, ParameterValue>* parameters)
{
    const auto& passes = builder.getPassDeclarations();
    const auto& resources = builder.getResourceDeclarations();
    VulkanCommandBuilder cmd(m_device, commandBuffer);
    if (m_stats) m_stats->beginRecording(commandBuffer);

    uint32_t execIdx = 0;
    for (uint32_t passIdx : passIndices) {
        const auto& compiledPass = compiled.compiledPasses[passIdx];
        const auto& passDecl = passes[compiledPass.declIndex];

        // Draws recorded from here to the end of the pass count towards it.
        RenderStatsCounting::active = m_stats ? m_stats->beginPass(passDecl.name) : nullptr;

        // Log execution order for debugging pass scheduling (throttled to every 5s)
        m_logger->logEvery(5.0f, LogLevel::Info, "  [pass %u/%u] '%s' (type=%s, exec=%s)",
            execIdx++, static_cast<uint32_t>(passIndices.size()),
            passDecl.name.c_str(),
            passDecl.type == PassType::Compute ? "compute" : "graphics",
            passDecl.execution.type.c_str());

        // Debug label for RenderDoc / validation layers
        cmd.beginDebugLabel(passDecl.name, {0.3f, 0.7f, 0.9f, 1.0f});

        // Notify debugger of pass begin
        if (m_debugger) {
            m_debugger->onPassBegin(passIdx, passDecl.name, commandBuffer);
        }

        // ── Queue ownership acquires, on their own: an acquire and a layout
        // transition of one image in one call would not be ordered ──
        if (!compiledPass.acquireBarriers.empty()) {
            recordBarriers(commandBuffer, compiledPass.acquireBarriers, nullptr, compiled, resources, false);
        }

        // ── Image barriers and buffer hazards, in one call ──
        recordBarriers(commandBuffer, compiledPass.preBarriers, &compiledPass.memoryBarrier,
                       compiled, resources, true);

        // ── Build execution context ──
        PassExecuteContext ctx(cmd);
        ctx.frameIndex = frameIndex;
        ctx.swapchainIndex = swapchainImageIndex;
        ctx.renderExtent = compiledPass.extent;
        ctx.physicalResourcesPtr = &compiled.physicalResources;
        ctx.physicalResourceCount = static_cast<uint32_t>(compiled.physicalResources.size());
        ctx.repeatIndex = passDecl.repeatIndex;
        ctx.repeatCount = passDecl.repeatCount;

        // Populate descriptor set and pipeline context
        if (!compiledPass.descriptorSets.empty()) {
            ctx.descriptorSets = &compiledPass.descriptorSets;
        }
        if (compiledPass.pipeline) {
            ctx.pipeline = compiledPass.pipeline.get();
            ctx.pipelineLayout = compiledPass.pipelineLayout;
        }
        if (compiledPass.computePipeline) {
            ctx.computePipeline = compiledPass.computePipeline.get();
            ctx.pipelineLayout = compiledPass.pipelineLayout;
        }

        // ── Begin rendering for graphics passes ──
        const VkRect2D area = passDecl.type == PassType::Graphics ? renderArea(passDecl, compiledPass)
                                                                  : VkRect2D{{0, 0}, compiledPass.extent};
        const bool rendering = passDecl.type == PassType::Graphics &&
                               beginRendering(commandBuffer, compiledPass, compiled, area, m_colorAttachments);

        // A disabled pass binds and records nothing. Its barriers above and
        // its rendering still run, so its attachments receive their clear
        // values and end in the layouts that later passes' barriers expect.
        // So does one whose viewport rectangle is empty this frame, and it
        // renders nothing either.
        const bool active = passDecl.enabled &&
                            (passDecl.type != PassType::Graphics || area.extent.width > 0);

        // ── Auto-bind pipeline if available ──
        if (active && passDecl.type == PassType::Graphics && compiledPass.pipeline) {
            cmd.bindPipeline(compiledPass.pipeline.get())
               .setViewport(ViewportState::fromRect(static_cast<float>(area.offset.x),
                                                    static_cast<float>(area.offset.y),
                                                    static_cast<float>(area.extent.width),
                                                    static_cast<float>(area.extent.height)))
               .setScissor(ScissorState::fromRect(area.offset.x, area.offset.y,
                                                  area.extent.width, area.extent.height));
        }
        else if (active && passDecl.type == PassType::Compute && compiledPass.computePipeline) {
            // Auto-bind compute pipeline before callback
            compiledPass.computePipeline->bind(commandBuffer);
        }

        // ── Execute pass (manual callback or auto-execution) ──
        if (!active) {
            // Nothing to record.
        } else if (passDecl.executeFn) {
            // Manual callback takes highest priority
            passDecl.executeFn(ctx);
        } else if (compiledPass.executionKind != ExecutionKind::None) {
            // Auto-execution based on execution.type
            executeAutoCallback(ctx, passDecl, compiledPass, compiled, builder, parameters, commandBuffer);
        } else {
            m_logger->logEvery(5.0f, LogLevel::Warning,
                               "Pass '%s' has no execute callback and execution.type is 'none'",
                               passDecl.name.c_str());
        }

        // ── End rendering, then post-barriers (e.g. to PRESENT_SRC_KHR) ──
        if (rendering) {
            vkCmdEndRendering(commandBuffer);
        }
        recordBarriers(commandBuffer, compiledPass.postBarriers, nullptr, compiled, resources, true);

        RenderStatsCounting::active = nullptr;
        if (m_stats) m_stats->endPass(commandBuffer);

        // Notify debugger of pass end
        if (m_debugger) {
            m_debugger->onPassEnd(passIdx, passDecl.name, commandBuffer);
        }

        cmd.endDebugLabel();
    }
}

// ═══════════════════════════════════════════════════════════════
// Auto-Execution Implementation
// 位先於動 — Position before action
// ═══════════════════════════════════════════════════════════════

void FrameGraphExecutor::executeAutoCallback(
    const PassExecuteContext& ctx,
    const PassDeclaration& passDecl,
    const CompiledPass& compiledPass,
    const FrameGraphCompiler::CompileResult& compiled,
    const FrameGraphBuilder& builder,
    const std::unordered_map<std::string, ParameterValue>* parameters,
    VkCommandBuffer commandBuffer)
{
    const auto& exec = passDecl.execution;

    // ── Auto-bind descriptor sets if enabled ──
    // Consecutive sets go down in one call; a missing set ends a run.
    if (exec.bindDescriptorSets && !compiledPass.descriptorSets.empty()) {
        VkPipelineBindPoint bindPoint = (passDecl.type == PassType::Compute)
            ? VK_PIPELINE_BIND_POINT_COMPUTE
            : VK_PIPELINE_BIND_POINT_GRAPHICS;

        uint32_t first = 0;
        m_descriptorSets.clear();
        auto flush = [&]() {
            if (!m_descriptorSets.empty()) {
                vkCmdBindDescriptorSets(commandBuffer, bindPoint, compiledPass.pipelineLayout, first,
                                        static_cast<uint32_t>(m_descriptorSets.size()),
                                        m_descriptorSets.data(), 0, nullptr);
                m_descriptorSets.clear();
            }
        };
        for (uint32_t i = 0; i < compiledPass.descriptorSets.size(); ++i) {
            const auto& set = compiledPass.descriptorSets[i];
            const bool bound = set && ctx.frameIndex < set->getSets().size();
            if (!bound) {
                flush();
                continue;
            }
            if (m_descriptorSets.empty()) first = i;
            m_descriptorSets.push_back(set->getSets()[ctx.frameIndex]);
        }
        flush();
    }

    // ── Auto-push constants from named parameters ──
    // A binding named pass.* reads this pass instead of a graph parameter.
    auto passValue = [&](const std::string& name) -> std::optional<ParameterValue> {
        if (name == "pass.repeatIndex") return ParameterValue{passDecl.repeatIndex};
        if (name == "pass.repeatCount") return ParameterValue{passDecl.repeatCount};
        const float w = static_cast<float>(ctx.renderExtent.width);
        const float h = static_cast<float>(ctx.renderExtent.height);
        if (name == "pass.extent") return ParameterValue{std::array<float, 2>{w, h}};
        if (name == "pass.texelSize") {
            return ParameterValue{std::array<float, 2>{w > 0.0f ? 1.0f / w : 0.0f, h > 0.0f ? 1.0f / h : 0.0f}};
        }
        return std::nullopt;
    };

    if (!passDecl.pushConstants.empty()) {
        for (size_t pci = 0; pci < passDecl.pushConstants.size(); ++pci) {
            const auto& pc = passDecl.pushConstants[pci];
            // Stage flags were resolved from the JSON strings at compile time
            const VkShaderStageFlags stageFlags = pci < compiledPass.pushConstantStages.size()
                ? compiledPass.pushConstantStages[pci]
                : JsonUtils::stringsToShaderStages(pc.stages);

            // Push each named binding
            for (const auto& binding : pc.bindings) {
                std::optional<ParameterValue> found = passValue(binding.name);
                if (!found && parameters) {
                    auto paramIt = parameters->find(binding.name);
                    if (paramIt != parameters->end()) found = paramIt->second;
                }
                if (!found) continue;

                const ParameterValue& value = *found;
                uint32_t offset = pc.offset + binding.offset;

                // Push based on type
                std::visit([&](const auto& v) {
                    vkCmdPushConstants(commandBuffer, compiledPass.pipelineLayout,
                        stageFlags, offset, sizeof(v), &v);
                }, value);
            }
        }
    }

    // ── Helper to look up resource dimensions ──
    auto getResourceExtent = [&](const std::string& resourceName) -> VkExtent2D {
        ResourceHandle handle = builder.getResource(resourceName);
        if (handle.valid() && handle.index < compiled.physicalResources.size()) {
            const auto* physImg = std::get_if<PhysicalImage>(&compiled.physicalResources[handle.index]);
            if (physImg) {
                return physImg->extent;
            }
        }
        // Fallback to render extent
        return ctx.renderExtent;
    };

    // ── Helper to look up parameter value as uint32_t ──
    auto getParameterAsUint = [&](const std::string& paramName, uint32_t defaultValue) -> uint32_t {
        if (!parameters || paramName.empty()) return defaultValue;
        auto it = parameters->find(paramName);
        if (it == parameters->end()) return defaultValue;

        // Try to extract uint32_t from the variant
        if (auto* v = std::get_if<uint32_t>(&it->second)) return *v;
        if (auto* v = std::get_if<int32_t>(&it->second)) return static_cast<uint32_t>(*v);
        if (auto* v = std::get_if<float>(&it->second)) return static_cast<uint32_t>(*v);
        return defaultValue;
    };

    // ── Execute based on type ──
    switch (compiledPass.executionKind) {
    case ExecutionKind::Fullscreen:
        // Fullscreen triangle draw
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        countDraw(3);
        break;

    case ExecutionKind::Draw: {
        // Custom draw with specified counts
        uint32_t vertexCount = exec.vertexCount.value;

        // Support parameter-based vertex count
        if (exec.vertexCount.isFromParameter()) {
            vertexCount = getParameterAsUint(exec.vertexCount.parameter, vertexCount);
            // Apply divisor if specified (e.g., for instanced rendering)
            if (exec.vertexCount.divisor > 1) {
                vertexCount = (vertexCount + exec.vertexCount.divisor - 1) / exec.vertexCount.divisor;
            }
        }

        m_logger->logEvery(5.0f, LogLevel::Info,
            "  [draw] pass='%s' vertexCount=%u instanceCount=%u",
            passDecl.name.c_str(), vertexCount, exec.instanceCount);

        vkCmdDraw(commandBuffer, vertexCount, exec.instanceCount,
                  exec.firstVertex, exec.firstInstance);
        countDraw(vertexCount, exec.instanceCount);
        break;
    }

    case ExecutionKind::ComputeDispatch:
    case ExecutionKind::ComputeImage: {
        // Calculate dispatch dimensions
        uint32_t groupX = 1, groupY = 1, groupZ = 1;

        if (compiledPass.executionKind == ExecutionKind::ComputeImage) {
            // Dispatch based on output image dimensions
            groupX = (ctx.renderExtent.width + exec.workgroupSize[0] - 1) / exec.workgroupSize[0];
            groupY = (ctx.renderExtent.height + exec.workgroupSize[1] - 1) / exec.workgroupSize[1];
            groupZ = 1;
        } else {
            // Use specified dispatch dimensions with proper resource lookup
            auto calcDim = [&](const DispatchDimension& dim) -> uint32_t {
                if (dim.isFixed()) {
                    return dim.value;
                } else if (dim.isFromParameter()) {
                    // Look up parameter value and apply divisor
                    uint32_t paramValue = getParameterAsUint(dim.parameter, 1);
                    return (paramValue + dim.divisor - 1) / dim.divisor;
                } else if (dim.isFromResource()) {
                    // Look up actual resource dimensions
                    VkExtent2D resExtent = getResourceExtent(dim.resource);
                    uint32_t size = (dim.dimension == "width") ? resExtent.width :
                                    (dim.dimension == "height") ? resExtent.height : 1;
                    return (size + dim.divisor - 1) / dim.divisor;
                } else {
                    return 1;
                }
            };

            groupX = calcDim(exec.dispatch[0]);
            groupY = calcDim(exec.dispatch[1]);
            groupZ = calcDim(exec.dispatch[2]);
        }

        m_logger->logEvery(5.0f, LogLevel::Info,
            "  [compute] pass='%s' dispatch=(%u, %u, %u)",
            passDecl.name.c_str(), groupX, groupY, groupZ);

        vkCmdDispatch(commandBuffer, groupX, groupY, groupZ);
        countDispatch();
        break;
    }

    case ExecutionKind::SceneRenderer:
        // Call scene renderer callback if registered. For the entity geometry
        // types RenderGraph auto-registers it, using the same
        // isEntityGeometryExecutionType list as executionKindOf.
        if (passDecl.sceneRendererFn) {
            passDecl.sceneRendererFn(ctx);
        } else {
            m_logger->logEvery(5.0f, LogLevel::Warning,
                "Pass '%s' has execution.type='%s' but no scene renderer registered",
                passDecl.name.c_str(), exec.type.c_str());
        }
        break;

    case ExecutionKind::Unknown:
        // Falling off the end of this chain used to be silent: a pass with a
        // typo'd or unsupported execution type simply drew nothing, with no
        // diagnostic anywhere. That is how sprite_geometry went unnoticed.
        m_logger->logEvery(5.0f, LogLevel::Warning,
            "Pass '%s' has unrecognised execution.type='%s' — nothing was recorded for it",
            passDecl.name.c_str(), exec.type.c_str());
        break;

    case ExecutionKind::None:
    case ExecutionKind::Manual:
        break;
    }
}

} // namespace FrameGraph
} // namespace Shoonyakasha
